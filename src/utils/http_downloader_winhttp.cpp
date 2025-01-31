#if PLUGIFY_PLATFORM_WINDOWS && PLUGIFY_DOWNLOADER

#include "http_downloader_winhttp.hpp"
#include "strings.hpp"

using namespace plugify;

HTTPDownloaderWinHttp::HTTPDownloaderWinHttp() : IHTTPDownloader() {
}

HTTPDownloaderWinHttp::~HTTPDownloaderWinHttp() {
	if (_hSession) {
		WinHttpSetStatusCallback(_hSession, nullptr, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0);
		WinHttpCloseHandle(_hSession);
	}
}

std::unique_ptr<IHTTPDownloader> IHTTPDownloader::Create(std::string userAgent) {
	auto instance = std::make_unique<HTTPDownloaderWinHttp>();
	if (!instance->Initialize(std::move(userAgent)))
		return {};
	return instance;
}

bool HTTPDownloaderWinHttp::Initialize(std::string userAgent) {
	static constexpr DWORD dwAccessType = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;

	_hSession = WinHttpOpen(String::ConvertUtf8ToWide(userAgent).c_str(), dwAccessType, nullptr, nullptr, WINHTTP_FLAG_ASYNC);
	if (_hSession == NULL) {
		PL_LOG_ERROR("WinHttpOpen() failed: {}", GetLastError());
		return false;
	}

	const DWORD notificationFlags = WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR | WINHTTP_CALLBACK_FLAG_HANDLES | WINHTTP_CALLBACK_FLAG_SECURE_FAILURE;
	if (WinHttpSetStatusCallback(_hSession, HTTPStatusCallback, notificationFlags, 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
		PL_LOG_ERROR("WinHttpSetStatusCallback() failed: {}", GetLastError());
		return false;
	}

	_userAgent = std::move(userAgent);

	return true;
}

void CALLBACK HTTPDownloaderWinHttp::HTTPStatusCallback(HINTERNET hRequest, DWORD_PTR dwContext, DWORD dwInternetStatus, LPVOID lpvStatusInformation, DWORD dwStatusInformationLength) {
	Request* req = reinterpret_cast<Request*>(dwContext);
	switch (dwInternetStatus) {
		case WINHTTP_CALLBACK_STATUS_HANDLE_CREATED:
			return;

		case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING: {
			if (!req)
				return;

			PL_ASSERT(hRequest == req->hRequest);

			auto parent = static_cast<HTTPDownloaderWinHttp*>(req->parent);
			std::unique_lock<std::mutex> lock(parent->_pendingRequestLock);
			PL_ASSERT(std::none_of(parent->_pendingRequests.begin(), parent->_pendingRequests.end(), [req](IHTTPDownloader::Request* it) { return it == req; }));

			// we can clean up the connection as well
			PL_ASSERT(req->hConnection != NULL);
			WinHttpCloseHandle(req->hConnection);
			delete req;
			return;
		}

		case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: {
			const WINHTTP_ASYNC_RESULT* res = reinterpret_cast<const WINHTTP_ASYNC_RESULT*>(lpvStatusInformation);
			PL_LOG_ERROR("WinHttp async function {} returned error {}", res->dwResult, res->dwError);
			req->statusCode = HTTP_STATUS_ERROR;
			req->state.store(Request::State::Complete);
			return;
		}

		case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE: {
			PL_LOG_VERBOSE("SendRequest complete");
			if (!WinHttpReceiveResponse(hRequest, nullptr)) {
				PL_LOG_ERROR("WinHttpReceiveResponse() failed: {}", GetLastError());
				req->statusCode = HTTP_STATUS_ERROR;
				req->state.store(Request::State::Complete);
			}
			return;
		}

		case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: {
			PL_LOG_VERBOSE("Headers available");

			DWORD bufferSize = sizeof(req->statusCode);
			if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
									 WINHTTP_HEADER_NAME_BY_INDEX, &req->statusCode, &bufferSize, WINHTTP_NO_HEADER_INDEX)) {
				PL_LOG_ERROR("WinHttpQueryHeaders() for status code failed: {}", GetLastError());
				req->statusCode = HTTP_STATUS_ERROR;
				req->state.store(Request::State::Complete);
				return;
			}

			bufferSize = sizeof(req->contentLength);
			if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
									 WINHTTP_HEADER_NAME_BY_INDEX, &req->contentLength, &bufferSize,
									 WINHTTP_NO_HEADER_INDEX)) {
				if (GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND)
					PL_LOG_WARNING("WinHttpQueryHeaders() for content length failed: {}", GetLastError());

				req->contentLength = 0;
			}

			DWORD contentTypeLength = 0;
			if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
									 WINHTTP_NO_OUTPUT_BUFFER, &contentTypeLength, WINHTTP_NO_HEADER_INDEX) &&
				GetLastError() == ERROR_INSUFFICIENT_BUFFER && contentTypeLength >= sizeof(contentTypeLength)) {
				std::wstring contentTypeWstring;
				contentTypeWstring.resize((contentTypeLength / sizeof(wchar_t)) - 1);
				if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
										contentTypeWstring.data(), &contentTypeLength, WINHTTP_NO_HEADER_INDEX)) {
					req->contentType = String::ConvertWideToUtf8(contentTypeWstring);
				}
			}

			PL_LOG_VERBOSE("Status code {}, content-length is {}", req->statusCode, req->contentLength);
			req->data.reserve(req->contentLength);
			req->state = Request::State::Receiving;

			// start reading
			if (!WinHttpQueryDataAvailable(hRequest, nullptr) && GetLastError() != ERROR_IO_PENDING) {
				PL_LOG_ERROR("WinHttpQueryDataAvailable() failed: {}", GetLastError());
				req->statusCode = HTTP_STATUS_ERROR;
				req->state.store(Request::State::Complete);
			}

			return;
		}

		case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE: {
			DWORD bytesAvailable;
			std::memcpy(&bytesAvailable, lpvStatusInformation, sizeof(bytesAvailable));
			if (bytesAvailable == 0) {
				// end of request
				PL_LOG_VERBOSE("End of request '{}', {} bytes received", req->url, req->data.size());
				req->state.store(Request::State::Complete);
				return;
			}

			// start the transfer
			PL_LOG_VERBOSE("{} bytes available", bytesAvailable);
			req->ioPosition = static_cast<uint32_t>(req->data.size());
			req->data.resize(req->ioPosition + bytesAvailable);
			if (!WinHttpReadData(hRequest, req->data.data() + req->ioPosition, bytesAvailable, nullptr) &&
				GetLastError() != ERROR_IO_PENDING) {
				PL_LOG_ERROR("WinHttpReadData() failed: {}", GetLastError());
				req->statusCode = HTTP_STATUS_ERROR;
				req->state.store(Request::State::Complete);
			}

			return;
		}

		case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: {
			PL_LOG_VERBOSE("Read of {} complete", dwStatusInformationLength);

			const uint32_t newSize = req->ioPosition + dwStatusInformationLength;
			PL_ASSERT(newSize <= req->data.size());
			req->data.resize(newSize);
			req->startTime = DateTime::Now();

			if (!WinHttpQueryDataAvailable(hRequest, nullptr) && GetLastError() != ERROR_IO_PENDING) {
				PL_LOG_ERROR("WinHttpQueryDataAvailable() failed: {}", GetLastError());
				req->statusCode = HTTP_STATUS_ERROR;
				req->state.store(Request::State::Complete);
			}

			return;
		}

		default:
			// unhandled, ignore
			return;
	}
}

plugify::IHTTPDownloader::Request* HTTPDownloaderWinHttp::InternalCreateRequest() {
	Request* req = new Request();
	return req;
}

void HTTPDownloaderWinHttp::InternalPollRequests() {
	// it uses windows's worker threads
}

bool HTTPDownloaderWinHttp::StartRequest(IHTTPDownloader::Request* request) {
	auto req = static_cast<Request*>(request);

	std::wstring hostName;
	hostName.resize(req->url.size());
	req->objectName.resize(req->url.size());

	URL_COMPONENTSW uc = {};
	uc.dwStructSize = sizeof(uc);
	uc.lpszHostName = hostName.data();
	uc.dwHostNameLength = static_cast<DWORD>(hostName.size());
	uc.lpszUrlPath = req->objectName.data();
	uc.dwUrlPathLength = static_cast<DWORD>(req->objectName.size());

	const std::wstring urlWide = String::ConvertUtf8ToWide(req->url);
	if (!WinHttpCrackUrl(urlWide.c_str(), static_cast<DWORD>(urlWide.size()), 0, &uc)) {
		PL_LOG_ERROR("WinHttpCrackUrl() failed: {}", GetLastError());
		req->callback(HTTP_STATUS_ERROR, {}, req->data);
		delete req;
		return false;
	}

	hostName.resize(uc.dwHostNameLength);
	req->objectName.resize(uc.dwUrlPathLength);

	req->hConnection = WinHttpConnect(_hSession, hostName.c_str(), uc.nPort, 0);
	if (!req->hConnection) {
		PL_LOG_ERROR("Failed to start HTTP request for '{}': {}", req->url, GetLastError());
		req->callback(HTTP_STATUS_ERROR, {}, req->data);
		delete req;
		return false;
	}

	const DWORD requestFlags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
	req->hRequest = WinHttpOpenRequest(req->hConnection, (req->type == IHTTPDownloader::Request::Type::Post) ? L"POST" : L"GET", req->objectName.c_str(), NULL, NULL, NULL, requestFlags);
	if (!req->hRequest) {
		PL_LOG_ERROR("WinHttpOpenRequest() failed: {}", GetLastError());
		WinHttpCloseHandle(req->hConnection);
		return false;
	}

	BOOL result;
	if (req->type == IHTTPDownloader::Request::Type::Post) {
		const std::wstring_view additionalHeaders = L"Content-Type: application/x-www-form-urlencoded\r\n";
		result = WinHttpSendRequest(req->hRequest, additionalHeaders.data(), static_cast<DWORD>(additionalHeaders.size()), req->postData.data(), static_cast<DWORD>(req->postData.size()), static_cast<DWORD>(req->postData.size()), reinterpret_cast<DWORD_PTR>(req));
	} else {
		result = WinHttpSendRequest(req->hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(req));
	}

	if (!result && GetLastError() != ERROR_IO_PENDING) {
		PL_LOG_ERROR("WinHttpSendRequest() failed: {}", GetLastError());
		req->statusCode = HTTP_STATUS_ERROR;
		req->state.store(Request::State::Complete);
	}

	PL_LOG_VERBOSE("Started HTTP request for '{}'", req->url);
	req->state = Request::State::Started;
	req->startTime = DateTime::Now();
	return true;
}

void HTTPDownloaderWinHttp::CloseRequest(IHTTPDownloader::Request* request) {
	auto req = static_cast<Request*>(request);

	if (req->hRequest != NULL) {
		// req will be freed by the callback.
		// the callback can fire immediately here if there's nothing running async, so don't touch req afterwards
		WinHttpCloseHandle(req->hRequest);
		return;
	}

	if (req->hConnection != NULL)
		WinHttpCloseHandle(req->hConnection);

	delete req;
}

#endif // PLUGIFY_PLATFORM_WINDOWS && PLUGIFY_DOWNLOADER
