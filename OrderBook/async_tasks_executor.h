#ifndef ALPHA_SERVER_ASYNC_TASKS_EXECUTOR_H
#define ALPHA_SERVER_ASYNC_TASKS_EXECUTOR_H

#if defined _MSC_VER && _MSC_VER >= 1020u
#pragma once
#endif

#include <mutex>

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread/scoped_thread.hpp>

namespace tools
{
	namespace async
	{
		class TasksExecutor
			: private boost::noncopyable
		{
			std::mutex _executor_state;

			std::unique_ptr<boost::asio::io_service> _service;
			std::unique_ptr<boost::asio::io_service::strand> _strand;
			std::unique_ptr<boost::asio::io_service::work> _work;
			boost::scoped_thread<boost::interrupt_and_join_if_joinable> _execution_thread;

		public:
			TasksExecutor() = default;
			~TasksExecutor() = default;

			void StartTasksExecution();
			void StopTasksExecution();

			decltype(*_strand) GetStrand() const;
		};
	}
} 

#endif