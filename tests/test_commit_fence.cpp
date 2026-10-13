#include "commit_fence.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace
{
void check( bool condition, const char *expression, int line )
{
	if ( condition )
		return;

	std::fprintf( stderr, "test_commit_fence.cpp:%d: CHECK(%s) failed\n", line, expression );
	std::exit( 1 );
}

#define CHECK( expression ) check( expression, #expression, __LINE__ )
}

int main()
{
	using gamescope::commit_fence::Action;
	using gamescope::commit_fence::alreadyReady;
	using gamescope::commit_fence::waitForFd;

	CHECK( alreadyReady().action == Action::Ready );

	// A numeric fd is never an implicit ready sentinel: every negative value
	// takes the loud error path.
	CHECK( waitForFd( -1 ).action == Action::Error );
	CHECK( waitForFd( -EBADF ).action == Action::Error );

	const int fenceFd = eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK );
	CHECK( fenceFd >= 0 );
	const auto fence = waitForFd( fenceFd );
	CHECK( fence.action == Action::Wait );
	CHECK( fence.fd == fenceFd );

	const int epollFd = epoll_create1( EPOLL_CLOEXEC );
	CHECK( epollFd >= 0 );
	epoll_event registration = {
		.events = EPOLLIN,
	};
	CHECK( epoll_ctl( epollFd, EPOLL_CTL_ADD, fence.fd, &registration ) == 0 );

	epoll_event event = {};
	CHECK( epoll_wait( epollFd, &event, 1, 0 ) == 0 );
	const eventfd_t signal = 1;
	CHECK( eventfd_write( fenceFd, signal ) == 0 );
	CHECK( epoll_wait( epollFd, &event, 1, 1000 ) == 1 );
	CHECK( ( event.events & EPOLLIN ) != 0 );

	close( epollFd );
	close( fenceFd );
	return 0;
}
