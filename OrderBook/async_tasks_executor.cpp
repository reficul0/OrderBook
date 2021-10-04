#include "pch.h"

namespace tools
{
	namespace async
	{
		void TasksExecutor::StartTasksExecution()
		{
			std::unique_lock<decltype(_executor_state)> executor_state_lock{_executor_state};

			_service = std::make_unique<decltype(_service)::element_type>();
			_strand = std::make_unique<decltype(_strand)::element_type>(*_service);
			_work = std::make_unique<decltype(_work)::element_type>(*_service);

			_execution_thread = decltype(_execution_thread){ 
				boost::thread(
					[](decltype(_strand) &strand)
					{
						try { strand->get_io_service().run(); }
						catch (boost::thread_interrupted const &)
						{
						}
					},
					std::ref(_strand)
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

		auto TasksExecutor::GetStrand() const
			-> decltype(*_strand)
		{
			return *_strand;
		}
	}
}
