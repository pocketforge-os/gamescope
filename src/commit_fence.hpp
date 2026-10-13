#pragma once

namespace gamescope::commit_fence
{
	enum class Action
	{
		Ready,
		Wait,
		Error,
	};

	struct Result
	{
		Action action;
		int fd = -1;
		int error = 0;
	};

	constexpr Result alreadyReady()
	{
		return { .action = Action::Ready };
	}

	constexpr Result waitForFd( int fd )
	{
		if ( fd < 0 )
			return { .action = Action::Error, .error = fd };

		return { .action = Action::Wait, .fd = fd };
	}

	constexpr Result error( int error )
	{
		return { .action = Action::Error, .error = error };
	}

	constexpr bool requiresPostEnqueueNudge( Action action )
	{
		return action == Action::Ready;
	}
}
