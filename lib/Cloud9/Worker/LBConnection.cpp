/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright (c) 2011, Dependable Systems Laboratory, EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
*/

#include "cloud9/worker/LBConnection.h"
#include "cloud9/worker/WorkerCommon.h"
#include "cloud9/worker/JobManager.h"
#include "cloud9/Logger.h"
#include "cloud9/Protocols.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"

#include <string>

using namespace llvm;
using namespace cloud9::data;

namespace {
  cl::opt<bool>
  DebugLBCommuncation("debug-lb-communication", cl::init(false));
}

namespace cloud9 {

namespace worker {

LBConnection::LBConnection(boost::asio::io_service &s, JobManager *jm) :
  service(s), socket(s), jobManager(jm), id(0) {

}

void LBConnection::connect(boost::system::error_code &error) {
  connectSocket(service, socket, LBAddress, LBPort, error);

}

LBConnection::~LBConnection() {
  socket.close();
}

void LBConnection::updateLogPrefix() {
  char prefix[] = "Worker<   >: ";
  sprintf(prefix, "Worker<%03d>: ", id);

  cloud9::Logger::getLogger().setLogPrefix(prefix);
}

void LBConnection::registerWorker() {
  // Prepare the registration message
  WorkerReportMessage message;
  message.set_id(0); // Don't have an ID yet

  WorkerReportMessage_Registration *reg = message.mutable_registration();
  reg->set_address(LocalAddress);
  reg->set_port(LocalPort);
  reg->set_wants_updates(UseGlobalCoverage);

  reg->set_prog_name(llvm::sys::path::parent_path(InputFile));
  reg->set_stat_id_count(jobManager->getCoverageIDCount());
  reg->set_prog_crc(jobManager->getModuleCRC());

  // Send the message
  std::string msgString;
  bool result = message.SerializeToString(&msgString);
  assert(result);

  sendMessage(socket, msgString);

  std::string respString;
  // Wait for the response
  recvMessage(socket, respString);

  LBResponseMessage response;
  result = response.ParseFromString(respString);
  assert(result);

  id = response.id();

  updateLogPrefix();

  CLOUD9_INFO("Worker registered with the load balancer - ID: " << id);

  processResponse(response);
}

void LBConnection::sendJobStatistics(WorkerReportMessage &message) {
  WorkerReportMessage_NodeDataUpdate *dataUpdate =
      message.mutable_nodedataupdate();
  std::vector<int> data;
  int total = 0;
  ExecutionPathSetPin paths = ExecutionPathSet::getEmptySet();

  jobManager->getStatisticsData(data, paths, true);

  //CLOUD9_DEBUG("Sending " << data.size() << " update values and " <<
  //		paths.size() << " update nodes");

  for (std::vector<int>::iterator it = data.begin(); it != data.end(); it++) {
    dataUpdate->add_data(*it);
    total += *it;
  }

  CLOUD9_DEBUG("[" << total << "] jobs reported to the load balancer");

  if (paths->count() > 0 || data.size() == 0) {
    assert(paths->count() == data.size());

    WorkerReportMessage_NodeSetUpdate *setUpdate =
        message.mutable_nodesetupdate();
    cloud9::data::ExecutionPathSet *pathSet = setUpdate->mutable_pathset();

    serializeExecutionPathSet(paths, *pathSet);
  }
}

void LBConnection::sendCoverageUpdates(WorkerReportMessage &message) {
  cov_update_t data;
  jobManager->getUpdatedLocalCoverage(data);

  if (data.size() > 0) {
    CLOUD9_DEBUG("Sending " << data.size() << " local coverage updates.");
    if (DebugLBCommuncation) {
      CLOUD9_DEBUG("Coverage updates sent: " << covUpdatesToString(data));
    }

    StatisticUpdate *update = message.add_localupdates();
    serializeStatisticUpdate(CLOUD9_STAT_NAME_LOCAL_COVERAGE, data, *update);
  }
}

void LBConnection::sendUpdates() {
  // Prepare the updates message
  WorkerReportMessage message;
  message.set_id(id);

  sendJobStatistics(message);

  sendCoverageUpdates(message);

  std::string msgString;
  bool result = message.SerializeToString(&msgString);
  assert(result);
  sendMessage(socket, msgString);

  std::string respString;
  recvMessage(socket, respString);

  LBResponseMessage response;
  result = response.ParseFromString(respString);
  assert(result);

  processResponse(response);
}

void LBConnection::processResponse(LBResponseMessage &response) {
  assert(id == response.id());

  if (response.more_details()) {
    jobManager->setRefineStatistics();
  }

  if (response.has_jobtransfer()) {
    CLOUD9_DEBUG("Job transfer request");

    const LBResponseMessage_JobTransfer &transDetails = response.jobtransfer();

    std::string destAddress = transDetails.dest_address();
    int destPort = transDetails.dest_port();

    ExecutionPathSetPin paths;
    std::vector<int> counts;

    paths = parseExecutionPathSet(transDetails.path_set());

    counts.insert(counts.begin(), transDetails.count().begin(),
        transDetails.count().end());

    transferJobs(destAddress, destPort, paths, counts);
  }

  if (response.has_jobseed()) {
    const LBResponseMessage_JobSeed &seedDetails = response.jobseed();

    const cloud9::data::ExecutionPathSet &pathSet = seedDetails.path_set();
    ExecutionPathSetPin paths;

    paths = parseExecutionPathSet(pathSet);

    CLOUD9_DEBUG("Job seed request: " << paths->count() << " paths");

    std::vector<long> replayInstrs;
    jobManager->importJobs(paths, replayInstrs);
  }

  if (response.terminate()) {
    jobManager->requestTermination();
  }

  if (UseGlobalCoverage) {
    for (int i = 0; i < response.globalupdates_size(); i++) {
      const StatisticUpdate &update = response.globalupdates(i);

      if (update.name() == CLOUD9_STAT_NAME_GLOBAL_COVERAGE) {
        cov_update_t data;

        parseStatisticUpdate(update, data);

        if (data.size() > 0) {
          CLOUD9_DEBUG("Receiving " << data.size() << " global coverage updates.");
          if (DebugLBCommuncation) {
            CLOUD9_DEBUG("Coverage updates received: " << covUpdatesToString(data));
          }

          jobManager->setUpdatedGlobalCoverage(data);
        }
      }
    }
  }
}

void LBConnection::transferJobs(std::string &destAddr, int destPort,
    ExecutionPathSetPin paths, std::vector<int> counts) {

  std::vector<long> replayInstrs;
  ExecutionPathSetPin jobPaths = jobManager->exportJobs(paths, counts,
      replayInstrs);

  tcp::socket peerSocket(service);
  boost::system::error_code error;

  connectSocket(service, peerSocket, destAddr, destPort, error);

  if (error) {
    CLOUD9_ERROR("Could not connect to the peer worker");
    return;
  }

  PeerTransferMessage message;
  cloud9::data::ExecutionPathSet *pSet = message.mutable_path_set();

  serializeExecutionPathSet(jobPaths, *pSet);

  for (std::vector<long>::iterator it = replayInstrs.begin(); it != replayInstrs.end(); it++) {
    message.add_instr_since_fork(*it);
  }

  std::string msgString;
  message.SerializeToString(&msgString);

  sendMessage(peerSocket, msgString);

  peerSocket.close();
}

}

}
