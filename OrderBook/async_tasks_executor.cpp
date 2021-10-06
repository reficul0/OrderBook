#include "pch.h"

namespace tools
{
	namespace async
	{
		void TasksExecutor::StartTasksExecution()
		{
			std::unique_lock<decltype(_executor_state)> executor_state_lock{_executor_state};

			_service = std::make_unique<decltype(_service)::element_type>();
			_work = std::make_unique<decltype(_work)::element_type>(*_service);

			_execution_thread = decltype(_execution_thread){ 
				boost::thread(
					[](decltype(_service) &service)
					{
						try { service->run(); }
						catch (boost::thread_interrupted const &)
						{
						}
					},
					std::ref(_service)
				)
			};
		}

		void TasksExecutor::StopTasksExecution()
		{
			std::unique_lock<decltype(_executor_state)> executor_state_lock{_executor_state};
			if (!_service)
				return;
			// пусть не сообщает сервису о задачах
			_work.reset();
			// остановим сервис
			_service->stop();
			// ждём когда поток исполнит текущую задачу и вернёт управлние из сервисного run
			_execution_thread = decltype(_execution_thread){};
			// удаляем все неисполненные задачи
			_service.reset();
		}

		auto TasksExecutor::GetService() const
			-> decltype(*_service)
		{
			return *_service;
		}
	}
}
