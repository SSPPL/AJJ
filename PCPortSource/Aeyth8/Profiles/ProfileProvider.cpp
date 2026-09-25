#include "ProfileProvider.h"

#include <Windows.h>
#include <winhttp.h>
#include <thread>
#include <future>
#include <chrono>
#include <mutex>
#include <format>

#include "../CmdArgs/CommandLineArgs.h"
#include "../Global.hpp"

#pragma comment(lib, "winhttp.lib")

using namespace A8CL;
using namespace Global;

/*

Written by Aeyth8

*/

namespace A8CL
{
namespace AJB
{

namespace
{
	// Bound on every network / file resolution, a stalled provider must never freeze a join.
	constexpr int32 ResolveTimeoutMs{5000};
	constexpr int32 HttpTimeoutMs{3000};

	std::wstring Utf8ToWideLocal(const std::string& In)
	{
		if (In.empty()) return std::wstring();

		const int Size = MultiByteToWideChar(CP_UTF8, 0, In.c_str(), static_cast<int>(In.size()), nullptr, 0);
		if (Size <= 0) return std::wstring();

		std::wstring Out(static_cast<size_t>(Size), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, In.c_str(), static_cast<int>(In.size()), Out.data(), Size);

		return Out;
	}

	// The reverse direction. Used for diagnostics only, the wire path always speaks UTF-8
	// bytes and never a narrow copy of a wide string.
	std::string WideToUtf8Local(const std::wstring& In)
	{
		if (In.empty()) return std::string();

		const int Size = WideCharToMultiByte(CP_UTF8, 0, In.c_str(), static_cast<int>(In.size()), nullptr, 0, nullptr, nullptr);
		if (Size <= 0) return std::string();

		std::string Out(static_cast<size_t>(Size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, In.c_str(), static_cast<int>(In.size()), Out.data(), Size, nullptr, nullptr);

		return Out;
	}

	class LocalJsonProfileProvider final : public IProfileProvider
	{
	public:
		const char* GetName() const override { return "LocalJsonProfileProvider"; }
		EProfileSource GetSource() const override { return EProfileSource::LocalJson; }

		ProfileResolveResponse Resolve(const ProfileResolveRequest& Request) override
		{
			ProfileResolveResponse Response{};
			Response.Source = EProfileSource::LocalJson;

			const ProfileParseResult Result = LoadLocalProfile(Request.AccountId);
			if (!Result.bSuccess)
			{
				Response.Error = Result.Error;
				return Response;
			}

			Response.bSuccess = true;
			Response.Record = Result.Record;
			Response.Record.ProfileToken = Request.ProfileToken;

			return Response;
		}
	};

	// Minimal WinHTTP GET/POST helper. Returns the body or an error string.
	// Splits "http://host:port" into its parts. Only plain http is accepted, the service is a
	// loopback fixture and https would need TLS flags this provider deliberately does not carry.
	bool ParseBaseUrl(const std::string& BaseUrl, std::wstring& OutScheme, std::wstring& OutHost, INTERNET_PORT& OutPort)
	{
		OutScheme.clear();
		OutHost.clear();
		OutPort = 0;

		const std::wstring Wide = Utf8ToWideLocal(BaseUrl);
		const size_t SchemeEnd = Wide.find(L"://");
		if (SchemeEnd == std::wstring::npos) return false;

		OutScheme = Wide.substr(0, SchemeEnd);

		std::wstring Remainder = Wide.substr(SchemeEnd + 3);
		if (Remainder.empty()) return false;

		const size_t Slash = Remainder.find(L'/');
		if (Slash != std::wstring::npos) Remainder = Remainder.substr(0, Slash);

		const size_t Colon = Remainder.rfind(L':');

		if (Colon != std::wstring::npos)
		{
			OutHost = Remainder.substr(0, Colon);

			const std::wstring PortText = Remainder.substr(Colon + 1);
			if (PortText.empty()) return false;

			try
			{
				const long Parsed = std::stol(PortText);
				if (Parsed <= 0 || Parsed > 65535) return false;

				OutPort = static_cast<INTERNET_PORT>(Parsed);
			}
			catch (const std::exception&)
			{
				return false;
			}
		}
		else
		{
			OutHost = Remainder;
			OutPort = 80;
		}

		return !OutHost.empty();
	}
	bool HttpRequest(const std::wstring& Verb, const std::wstring& Host, const std::wstring& Path, INTERNET_PORT Port,
		const std::string& Body, std::string& OutResponse, std::string& OutError)
	{
		HINTERNET Session = WinHttpOpen(L"AJB PC Port Profile Client/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!Session)
		{
			OutError = "WinHttpOpen failed";
			return false;
		}

		WinHttpSetTimeouts(Session, HttpTimeoutMs, HttpTimeoutMs, HttpTimeoutMs, HttpTimeoutMs);

		HINTERNET Connection = WinHttpConnect(Session, Host.c_str(), Port, 0);
		if (!Connection)
		{
			OutError = "WinHttpConnect failed";
			WinHttpCloseHandle(Session);
			return false;
		}

		HINTERNET Request = WinHttpOpenRequest(Connection, Verb.c_str(), Path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
		if (!Request)
		{
			OutError = "WinHttpOpenRequest failed";
			WinHttpCloseHandle(Connection);
			WinHttpCloseHandle(Session);
			return false;
		}

		const wchar_t* Headers = Body.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : L"Content-Type: application/json\r\n";

		const BOOL bSent = WinHttpSendRequest(Request, Headers, -1L,
			Body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(Body.data()),
			static_cast<DWORD>(Body.size()), static_cast<DWORD>(Body.size()), 0);

		if (!bSent)
		{
			OutError = "WinHttpSendRequest failed";
			WinHttpCloseHandle(Request);
			WinHttpCloseHandle(Connection);
			WinHttpCloseHandle(Session);
			return false;
		}

		if (!WinHttpReceiveResponse(Request, nullptr))
		{
			OutError = "WinHttpReceiveResponse failed";
			WinHttpCloseHandle(Request);
			WinHttpCloseHandle(Connection);
			WinHttpCloseHandle(Session);
			return false;
		}

		DWORD StatusCode{0};
		DWORD StatusSize = sizeof(StatusCode);
		WinHttpQueryHeaders(Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &StatusCode, &StatusSize, WINHTTP_NO_HEADER_INDEX);

		std::string ResponseBody;
		for (;;)
		{
			DWORD BytesAvailable{0};
			if (!WinHttpQueryDataAvailable(Request, &BytesAvailable)) break;
			if (BytesAvailable == 0) break;

			std::string Chunk(BytesAvailable, '\0');
			DWORD BytesRead{0};
			if (!WinHttpReadData(Request, Chunk.data(), BytesAvailable, &BytesRead)) break;

			Chunk.resize(BytesRead);
			ResponseBody += Chunk;
		}

		WinHttpCloseHandle(Request);
		WinHttpCloseHandle(Connection);
		WinHttpCloseHandle(Session);

		if (StatusCode < 200 || StatusCode >= 300)
		{
			OutError = std::format("HTTP {}", StatusCode);
			return false;
		}

		OutResponse = ResponseBody;

		return true;
	}

	class HttpProfileProvider final : public IProfileProvider
	{
	public:
		explicit HttpProfileProvider(std::string InBaseUrl) : BaseUrl(std::move(InBaseUrl)) {}

		const char* GetName() const override { return "HttpProfileProvider"; }
		EProfileSource GetSource() const override { return EProfileSource::Http; }

		ProfileResolveResponse Resolve(const ProfileResolveRequest& Request) override
		{
			ProfileResolveResponse Response{};
			Response.Source = EProfileSource::Http;

			if (Request.ProfileToken.empty())
			{
				Response.Error = "no profile token was supplied";
				return Response;
			}

			std::wstring Host;
			std::wstring Scheme;
			INTERNET_PORT Port{0};

			if (!ParseBaseUrl(BaseUrl, Scheme, Host, Port))
			{
				Response.Error = std::format("the configured base url '{}' is not usable", BaseUrl);
				return Response;
			}

			if (Scheme != L"http")
			{
				// The service is a plain HTTP loopback fixture, an https url would need TLS flags
				// that are deliberately not part of this provider.
				Response.Error = std::format("unsupported url scheme '{}', only http is supported", WideToUtf8Local(Scheme));
				return Response;
			}

			const std::wstring Path = L"/profile?token=" + Utf8ToWideLocal(Request.ProfileToken);

			std::string Body;
			if (!HttpRequest(L"GET", Host, Path, Port, std::string(), Body, Response.Error))
			{
				return Response;
			}

			const ProfileParseResult Result = ParseProfileJson(Body, Request.AccountId);
			if (!Result.bSuccess)
			{
				Response.Error = Result.Error;
				return Response;
			}

			Response.bSuccess = true;
			Response.Record = Result.Record;
			Response.Record.ProfileToken = Request.ProfileToken;

			return Response;
		}

	private:
		std::string BaseUrl;
	};
}

std::unique_ptr<IProfileProvider> MakeLocalJsonProfileProvider()
{
	return std::make_unique<LocalJsonProfileProvider>();
}

std::unique_ptr<IProfileProvider> MakeHttpProfileProvider(const std::string& BaseUrl)
{
	return std::make_unique<HttpProfileProvider>(BaseUrl);
}

EProfileSource GetConfiguredProfileSource()
{
	const wchar_t* Argument = CMLA::ProfileSource.GetArgumentAsString();

	if (Argument && CMLA::ProfileSource.HasChanged())
	{
		const std::wstring Value{ Argument };

		if (Value == L"http") return EProfileSource::Http;
		if (Value == L"local") return EProfileSource::LocalJson;
		if (Value == L"disabled") return EProfileSource::Disabled;
	}

	// Default policy: the local JSON file is authoritative when present, the HTTP service is
	// the fallback. Both produce the same ProfileRecord and the rest of the code cannot tell
	// which one answered.
	return EProfileSource::LocalJson;
}

ProfileResolveResponse ResolveProfileBlocking(const ProfileResolveRequest& Request)
{
	const EProfileSource Source = GetConfiguredProfileSource();

	if (Source == EProfileSource::Disabled)
	{
		ProfileResolveResponse Response{};
		Response.Error = "profile resolution is disabled";
		return Response;
	}

	// The provider does its work on a worker thread and reports a plain C++ result, the game
	// thread only ever consumes the outcome.
	auto ResolveWith = [&Request](std::unique_ptr<IProfileProvider> Provider) -> ProfileResolveResponse
	{
		if (!Provider)
		{
			ProfileResolveResponse Response{};
			Response.Error = "provider could not be created";
			return Response;
		}

		std::packaged_task<ProfileResolveResponse()> Task([&Request, &Provider]() { return Provider->Resolve(Request); });
		std::future<ProfileResolveResponse> Future = Task.get_future();

		std::thread Worker(std::move(Task));
		Worker.detach();

		if (Future.wait_for(std::chrono::milliseconds(ResolveTimeoutMs)) != std::future_status::ready)
		{
			ProfileResolveResponse Response{};
			Response.Error = std::format("profile resolution timed out after {}ms", ResolveTimeoutMs);
			return Response;
		}

		return Future.get();
	};

	ProfileResolveResponse Local{};

	if (Source == EProfileSource::LocalJson)
	{
		Local = ResolveWith(MakeLocalJsonProfileProvider());
		if (Local.bSuccess) return Local;

		LogA("ResolveProfileBlocking", std::format("[AccountId]: {} | [LocalJson]: {} | [Fallback]: HTTP", Request.AccountId, Local.Error));
	}
	else if (Source == EProfileSource::Http)
	{
		return ResolveWith(MakeHttpProfileProvider());
	}

	ProfileResolveResponse Http = ResolveWith(MakeHttpProfileProvider());
	if (Http.bSuccess) return Http;

	// Both providers failed. Report the failure, never invent a default profile.
	LogA("ResolveProfileBlocking", std::format("[AccountId]: {} | [Http]: {}", Request.AccountId, Http.Error));

	Http.Error = std::format("local JSON: {} | HTTP: {}", Local.Error, Http.Error);

	return Http;
}

namespace
{
	// Runs the configured provider chain on a worker thread and reports a plain C++ result.
	ProfileResolveResponse ResolveOnWorker(const ProfileResolveRequest& Request)
	{
		const EProfileSource Source = GetConfiguredProfileSource();

		if (Source == EProfileSource::Disabled)
		{
			ProfileResolveResponse Response{};
			Response.Error = "profile resolution is disabled";
			return Response;
		}

		ProfileResolveResponse Local{};

		if (Source == EProfileSource::LocalJson)
		{
			Local = MakeLocalJsonProfileProvider()->Resolve(Request);
			if (Local.bSuccess) return Local;

			LogA("ResolveProfile", std::format("[AccountId]: {} | [LocalJson]: {} | [Fallback]: HTTP", Request.AccountId, Local.Error));
		}
		else if (Source == EProfileSource::Http)
		{
			return MakeHttpProfileProvider()->Resolve(Request);
		}

		ProfileResolveResponse Http = MakeHttpProfileProvider()->Resolve(Request);
		if (Http.bSuccess) return Http;

		// Both providers failed. Report the failure, never invent a default profile.
		LogA("ResolveProfile", std::format("[AccountId]: {} | [Http]: {}", Request.AccountId, Http.Error));

		Http.Error = std::format("local JSON: {} | HTTP: {}", Local.Error, Http.Error);

		return Http;
	}
}

AsyncProfileResolve StartProfileResolve(const ProfileResolveRequest& Request)
{
	AsyncProfileResolve Handle{};
	Handle.StartedAt = std::chrono::steady_clock::now();

	if (GetConfiguredProfileSource() == EProfileSource::Disabled)
	{
		return Handle;
	}

	// shared_future so the handle stays copyable inside the connection table.
	Handle.Future = std::async(std::launch::async, [Request]() { return ResolveOnWorker(Request); }).share();
	Handle.bValid = true;

	return Handle;
}

bool PollProfileResolve(AsyncProfileResolve& Handle, ProfileResolveResponse& OutResponse, bool& bOutTimedOut)
{
	bOutTimedOut = false;

	if (!Handle.bValid) return false;

	if (Handle.Future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		OutResponse = Handle.Future.get();
		Handle.bValid = false;
		return true;
	}

	const auto Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - Handle.StartedAt);
	if (Elapsed.count() >= ResolveTimeoutMs)
	{
		bOutTimedOut = true;
		Handle.bValid = false;
	}

	return false;
}

namespace
{
	// Shared state for the one-shot login token acquisition. A worker thread writes it once,
	// the game thread only reads it, so a mutex is enough and no join ever blocks.
	struct ProfileTokenState
	{
		std::mutex Guard;
		std::string Token;
		std::string Status{"not started"};
		bool bFinished{false};
	};

	ProfileTokenState& TokenState()
	{
		static ProfileTokenState State;
		return State;
	}

	// Lifts one string field out of the small /login response.
	std::string ExtractJsonString(const std::string& Body, const std::string& Field)
	{
		const std::string Needle = "\"" + Field + "\"";
		size_t Position = Body.find(Needle);
		if (Position == std::string::npos) return std::string();

		Position = Body.find(':', Position + Needle.size());
		if (Position == std::string::npos) return std::string();

		const size_t Open = Body.find('"', Position);
		if (Open == std::string::npos) return std::string();

		const size_t Close = Body.find('"', Open + 1);
		if (Close == std::string::npos) return std::string();

		return Body.substr(Open + 1, Close - Open - 1);
	}
}

void BeginProfileTokenAcquisition(const std::string& AccountId)
{
	if (AccountId.empty())
	{
		LogA("ProfileToken", "[Error]: no account id, the HTTP login was not attempted.");
		return;
	}

	{
		ProfileTokenState& State = TokenState();

		std::lock_guard<std::mutex> Lock(State.Guard);
		if (State.bFinished || State.Status == "in progress")
		{
			return;
		}
		State.Status = "in progress";
	}

	// The arcade password is unavailable locally so the local service is expected to issue a
	// token for any known account. This placeholder credential is only ever used for that.
	std::string RequestBody = std::format("{{\"account_id\":\"{}\",\"password\":\"local-test\"}}", AccountId);

	// A detached worker, never a discarded std::async future: dropping a future would block
	// the game thread until the network call finished, which is exactly what must not happen.
	std::thread([AccountId, RequestBody]()
	{
		std::wstring Scheme;
		std::wstring Host;
		INTERNET_PORT Port{0};

		std::string Token;
		std::string Status;

		if (!ParseBaseUrl("http://127.0.0.1:8080", Scheme, Host, Port))
		{
			Status = "the default service url is not usable";
		}
		else
		{
			std::string Body;
			std::string Error;

			if (!HttpRequest(L"POST", Host, L"/login", Port, RequestBody, Body, Error))
			{
				Status = Error;
			}
			else
			{
				Token = ExtractJsonString(Body, "token");

				if (Token.empty())
				{
					Status = "the login response carried no token";
				}
				else
				{
					Status = "acquired";
				}
			}
		}

		ProfileTokenState& State = TokenState();

		std::lock_guard<std::mutex> Lock(State.Guard);
		State.Token = Token;
		State.Status = Status;
		State.bFinished = true;

		LogA("ProfileToken", std::format("[AccountId]: {} | [State]: {} | [TokenLength]: {}", AccountId, Status, Token.size()));
	}).detach();

	LogA("ProfileToken", std::format("[AccountId]: {} | [State]: acquisition started", AccountId));
}

std::string GetAcquiredProfileToken()
{
	ProfileTokenState& State = TokenState();

	std::lock_guard<std::mutex> Lock(State.Guard);
	return State.Token;
}

std::string GetProfileTokenStatus()
{
	ProfileTokenState& State = TokenState();

	std::lock_guard<std::mutex> Lock(State.Guard);
	return State.Status;
}

}
}
