#pragma once
#include "ProfileRecord.h"

#include <functional>
#include <chrono>
#include <future>
#include <memory>
#include <string>

/*

Written by Aeyth8

*/

// Profile providers. A provider is always invoked on a worker thread and only ever touches
// plain C++ data, it must never reach for a Unreal object. The caller applies the result to
// the game on the game thread.

namespace A8CL
{
namespace AJB
{

	struct ProfileResolveRequest
	{
		std::string AccountId;
		std::string ProfileToken;
	};

	struct ProfileResolveResponse
	{
		bool bSuccess{false};
		ProfileRecord Record{};
		std::string Error;
		EProfileSource Source{EProfileSource::Disabled};
	};

	class IProfileProvider
	{
	public:
		virtual ~IProfileProvider() = default;

		virtual const char* GetName() const = 0;
		virtual EProfileSource GetSource() const = 0;

		// Blocking, worker thread only.
		virtual ProfileResolveResponse Resolve(const ProfileResolveRequest& Request) = 0;
	};

	// Reads <GameDir>\Aeyth8\Configs\profiles\<account_id>.json.
	std::unique_ptr<IProfileProvider> MakeLocalJsonProfileProvider();

	// WinHTTP client for the local ProfileService. BaseUrl defaults to http://127.0.0.1:8080.
	std::unique_ptr<IProfileProvider> MakeHttpProfileProvider(const std::string& BaseUrl = "http://127.0.0.1:8080");

	// Parses the -ProfileSource= argument. Defaults to local-first with an HTTP fallback.
	EProfileSource GetConfiguredProfileSource();

	// Resolves a profile off the game thread and blocks until it finishes or the bounded
	// timeout expires. Returns a failure response instead of a fabricated default profile.
	ProfileResolveResponse ResolveProfileBlocking(const ProfileResolveRequest& Request);

	// Non blocking variant used by the join flow. The provider runs on a worker thread and the
	// caller polls from the tick; the game thread never waits on file or network IO.
	struct AsyncProfileResolve
	{
		std::shared_future<ProfileResolveResponse> Future;
		std::chrono::steady_clock::time_point StartedAt{};
		bool bValid{false};
	};

	// Starts the resolution. Returns an invalid handle when the provider is disabled.
	AsyncProfileResolve StartProfileResolve(const ProfileResolveRequest& Request);

	// True when the worker finished. OutResponse is only written on success. Reports timeout
	// through bOutTimedOut so the caller can drop the connection with a logged reason.
	bool PollProfileResolve(AsyncProfileResolve& Handle, ProfileResolveResponse& OutResponse, bool& bOutTimedOut);

	// Phase 6: the HTTP provider needs a short lived token from POST /login before a profile
	// can be read. The acquisition runs on a worker thread at start up and the game thread only
	// reads the result, so a join never waits on the network.
	void BeginProfileTokenAcquisition(const std::string& AccountId);

	// Returns the acquired token or an empty string when none is ready yet.
	std::string GetAcquiredProfileToken();

	// Human readable status of the token acquisition, for the console and the logs.
	std::string GetProfileTokenStatus();

}
}
