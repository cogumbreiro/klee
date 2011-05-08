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

#ifndef LBCONNECTION_H_
#define LBCONNECTION_H_

#include "cloud9/Protocols.h"

#include <boost/asio.hpp>

using namespace boost::asio::ip;
using namespace cloud9::data;

namespace cloud9 {

namespace worker {

class JobManager;

class LBConnection {
private:
	boost::asio::io_service &service;

	tcp::socket socket;

	JobManager *jobManager;

	unsigned int id; // The worker ID assigned by the load balancer

	void transferJobs(std::string &destAddr, int destPort,
			ExecutionPathSetPin paths,
			std::vector<int> counts);

	void processResponse(LBResponseMessage &response);

	void updateLogPrefix();

	void sendJobStatistics(WorkerReportMessage &message);
	void sendCoverageUpdates(WorkerReportMessage &message);

public:
	LBConnection(boost::asio::io_service &service, JobManager *jobManager);
	virtual ~LBConnection();

	void connect(boost::system::error_code &error);

	void registerWorker();

	void sendUpdates();
};

}

}
#endif /* LBCONNECTION_H_ */
