#define LOG_MODULE PacketLogModuleHttpLayer

#include "Logger.h"
#include "GeneralUtils.h"
#include "HttpLayer.h"
#include <string.h>
#include <algorithm>
#include <stdlib.h>
#include <exception>
#include <utility>
#include <unordered_map>

namespace pcpp
{


// -------- Class HttpMessage -----------------


HeaderField* HttpMessage::addField(const std::string& fieldName, const std::string& fieldValue)
{
	if (getFieldByName(fieldName) != nullptr)
	{
		PCPP_LOG_ERROR("Field '" << fieldName << "' already exists!");
		return nullptr;
	}

	return TextBasedProtocolMessage::addField(fieldName, fieldValue);
}

HeaderField* HttpMessage::addField(const HeaderField& newField)
{
	if (getFieldByName(newField.getFieldName()) != nullptr)
	{
		PCPP_LOG_ERROR("Field '" << newField.getFieldName() << "' already exists!");
		return nullptr;
	}

	return TextBasedProtocolMessage::addField(newField);
}

HeaderField* HttpMessage::insertField(HeaderField* prevField, const std::string& fieldName, const std::string& fieldValue)
{
	if (getFieldByName(fieldName) != nullptr)
	{
		PCPP_LOG_ERROR("Field '" << fieldName << "' already exists!");
		return nullptr;
	}

	return TextBasedProtocolMessage::insertField(prevField, fieldName, fieldValue);
}

HeaderField* HttpMessage::insertField(HeaderField* prevField, const HeaderField& newField)
{
	if (getFieldByName(newField.getFieldName()) != nullptr)
	{
		PCPP_LOG_ERROR("Field '" << newField.getFieldName() << "' already exists!");
		return nullptr;
	}

	return TextBasedProtocolMessage::insertField(prevField, newField);
}



// -------- Class HttpRequestLayer -----------------

HttpRequestLayer::HttpRequestLayer(uint8_t* data, size_t dataLen, Layer* prevLayer, Packet* packet) : HttpMessage(data, dataLen, prevLayer, packet)
{
	m_Protocol = HTTPRequest;
	m_FirstLine = new HttpRequestFirstLine(this);
	m_FieldsOffset = m_FirstLine->getSize();
	parseFields();
}

HttpRequestLayer::HttpRequestLayer(HttpMethod method, const std::string& uri, HttpVersion version)
{
	m_Protocol = HTTPRequest;
	m_FirstLine = new HttpRequestFirstLine(this, method, version, uri);
	m_FieldsOffset = m_FirstLine->getSize();
}

HttpRequestLayer::HttpRequestLayer(const HttpRequestLayer& other) : HttpMessage(other)
{
	m_FirstLine = new HttpRequestFirstLine(this);
}

HttpRequestLayer& HttpRequestLayer::operator=(const HttpRequestLayer& other)
{
	HttpMessage::operator=(other);

	if (m_FirstLine != nullptr)
		delete m_FirstLine;

	m_FirstLine = new HttpRequestFirstLine(this);

	return *this;
}


std::string HttpRequestLayer::getUrl() const
{
	HeaderField* hostField = getFieldByName(PCPP_HTTP_HOST_FIELD);
	if (hostField == nullptr)
		return m_FirstLine->getUri();

	return hostField->getFieldValue() + m_FirstLine->getUri();
}

HttpRequestLayer::~HttpRequestLayer()
{
	delete m_FirstLine;
}

std::string HttpRequestLayer::toString() const
{
	static const int maxLengthToPrint = 120;
	std::string result = "HTTP request, ";
	int size = m_FirstLine->getSize() - 2; // the -2 is to remove \r\n at the end of the first line
	if (size <= 0)
	{
		result += std::string("CORRUPT DATA");
		return result;
	}
	if (size <= maxLengthToPrint)
	{
		char* firstLine = new char[size+1];
		strncpy(firstLine, (char*)m_Data, size);
		firstLine[size] = 0;
		result += std::string(firstLine);
		delete[] firstLine;
	}
	else
	{
		char firstLine[maxLengthToPrint+1];
		strncpy(firstLine, (char*)m_Data, maxLengthToPrint-3);
		firstLine[maxLengthToPrint-3] = '.';
		firstLine[maxLengthToPrint-2] = '.';
		firstLine[maxLengthToPrint-1] = '.';
		firstLine[maxLengthToPrint] = 0;
		result += std::string(firstLine);
	}

	return result;
}







// -------- Class HttpRequestFirstLine -----------------


const std::string MethodEnumToString[9] = {
		"GET",
		"HEAD",
		"POST",
		"PUT",
		"DELETE",
		"TRACE",
		"OPTIONS",
		"CONNECT",
		"PATCH"
};

const std::unordered_map<std::string, HttpRequestLayer::HttpMethod> HttpMethodStringToEnum {
		{"GET", HttpRequestLayer::HttpMethod::HttpGET },
		{"HEAD", HttpRequestLayer::HttpMethod::HttpHEAD },
		{"POST", HttpRequestLayer::HttpMethod::HttpPOST },
		{"PUT", HttpRequestLayer::HttpMethod::HttpPUT },
		{"DELETE", HttpRequestLayer::HttpMethod::HttpDELETE },
		{"TRACE", HttpRequestLayer::HttpMethod::HttpTRACE },
		{"OPTIONS", HttpRequestLayer::HttpMethod::HttpOPTIONS },
		{"CONNECT", HttpRequestLayer::HttpMethod::HttpCONNECT },
		{"PATCH", HttpRequestLayer::HttpMethod::HttpPATCH }
};

const std::string VersionEnumToString[3] = {
		"0.9",
		"1.0",
		"1.1"
};

const std::unordered_map<std::string, HttpVersion> HttpVersionStringToEnum {
		{ "0.9", HttpVersion::ZeroDotNine },
		{ "1.0", HttpVersion::OneDotZero },
		{ "1.1", HttpVersion::OneDotOne }
};


HttpRequestFirstLine::HttpRequestFirstLine(HttpRequestLayer* httpRequest) : m_HttpRequest(httpRequest)
{
	m_Method = parseMethod((char*)m_HttpRequest->m_Data, m_HttpRequest->getDataLen());
	if (m_Method == HttpRequestLayer::HttpMethodUnknown)
	{
		m_UriOffset = -1;
		PCPP_LOG_DEBUG("Couldn't resolve HTTP request method");
		m_IsComplete = false;
		m_Version = HttpVersionUnknown;
		m_VersionOffset = -1;
		m_FirstLineEndOffset = m_HttpRequest->getDataLen();
		return;
	}
	else
		m_UriOffset = MethodEnumToString[m_Method].length() + 1;

	parseVersion();
	if(m_VersionOffset < 0)
	{
		m_IsComplete = false;
		m_FirstLineEndOffset = m_HttpRequest->getDataLen();
		return;
	}

	char* endOfFirstLine;
	if ((endOfFirstLine = (char*)memchr((char*)(m_HttpRequest->m_Data + m_VersionOffset), '\n', m_HttpRequest->m_DataLen-(size_t)m_VersionOffset)) != nullptr)
	{
		m_FirstLineEndOffset = endOfFirstLine - (char*)m_HttpRequest->m_Data + 1;
		m_IsComplete = true;
	}
	else
	{
		m_FirstLineEndOffset = m_HttpRequest->getDataLen();
		m_IsComplete = false;
	}

	if (Logger::getInstance().isDebugEnabled(PacketLogModuleHttpLayer))
	{
		std::string method = m_Method == HttpRequestLayer::HttpMethodUnknown? "Unknown" : MethodEnumToString[m_Method];
		PCPP_LOG_DEBUG(
			"Method='" << method << "'; "
			<< "HTTP version='" << VersionEnumToString[m_Version] << "'; "
			<< "URI='" << getUri() << "'");
	}
}

HttpRequestFirstLine::HttpRequestFirstLine(HttpRequestLayer* httpRequest, HttpRequestLayer::HttpMethod method, HttpVersion version, const std::string &uri)
{
	try		// throw(HttpRequestFirstLineException)
	{
		if (method == HttpRequestLayer::HttpMethodUnknown)
		{
			m_Exception.setMessage("Method supplied was HttpMethodUnknown");
			throw m_Exception;
		}

		if (version == HttpVersionUnknown)
		{
			m_Exception.setMessage("Version supplied was HttpVersionUnknown");
			throw m_Exception;
		}

		m_HttpRequest = httpRequest;

		m_Method = method;
		m_Version = version;

		std::string firstLine = MethodEnumToString[m_Method] + " " + uri + " "  + "HTTP/" + VersionEnumToString[m_Version] + "\r\n";

		m_UriOffset =  MethodEnumToString[m_Method].length() + 1;
		m_FirstLineEndOffset = firstLine.length();
		m_VersionOffset = m_UriOffset + uri.length() + 6;

		m_HttpRequest->m_DataLen = firstLine.length();
		m_HttpRequest->m_Data = new uint8_t[m_HttpRequest->m_DataLen];
		memcpy(m_HttpRequest->m_Data, firstLine.c_str(), m_HttpRequest->m_DataLen);

		m_IsComplete = true;
	}
	catch(const HttpRequestFirstLineException&)
	{
		throw;
	}
	catch(...)
	{
		std::terminate();
	}
}

HttpRequestLayer::HttpMethod HttpRequestFirstLine::parseMethod(const char* data, size_t dataLen)
{
	if (!data || dataLen < 4)
	{
		return HttpRequestLayer::HttpMethodUnknown;
	}

	size_t spaceIndex = 0;
	while (spaceIndex < dataLen && data[spaceIndex] != ' ' )
	{
		spaceIndex++;
	}

	if (spaceIndex == 0 || spaceIndex == dataLen)
	{
		return HttpRequestLayer::HttpMethodUnknown;
	}

	auto methodAdEnum = HttpMethodStringToEnum.find(std::string(data, data + spaceIndex));
	if (methodAdEnum == HttpMethodStringToEnum.end())
	{
		return HttpRequestLayer::HttpMethodUnknown;
	}
	return methodAdEnum->second;
}

void HttpRequestFirstLine::parseVersion()
{
	char* data = (char*)(m_HttpRequest->m_Data + m_UriOffset);
	char* verPos = cross_platform_memmem(data, m_HttpRequest->getDataLen() - m_UriOffset, " HTTP/", 6);
	if (verPos == nullptr)
	{
		m_Version = HttpVersionUnknown;
		m_VersionOffset = -1;
		return;
	}

	// verify packet doesn't end before the version, meaning still left place for " HTTP/x.y" (9 chars)
	std::ptrdiff_t actualLen = verPos + 9 - (char*)m_HttpRequest->m_Data;
	if (static_cast<size_t>(actualLen) > m_HttpRequest->getDataLen())
	{
		m_Version = HttpVersionUnknown;
		m_VersionOffset = -1;
		return;
	}

	//skip " HTTP/" (6 chars)
	verPos += 6;
	auto versionAsEnum = HttpVersionStringToEnum.find(std::string(verPos, verPos + 3));
	if (versionAsEnum == HttpVersionStringToEnum.end())
	{
		m_Version = HttpVersionUnknown;
	}
	else
	{
		m_Version = versionAsEnum->second;
	}

	m_VersionOffset = verPos - (char*)m_HttpRequest->m_Data;
}

bool HttpRequestFirstLine::setMethod(HttpRequestLayer::HttpMethod newMethod)
{
	if (newMethod == HttpRequestLayer::HttpMethodUnknown)
	{
		PCPP_LOG_ERROR("Requested method is HttpMethodUnknown");
		return false;
	}

	//extend or shorten layer
	int lengthDifference = MethodEnumToString[newMethod].length() - MethodEnumToString[m_Method].length();
	if (lengthDifference > 0)
	{
		if (!m_HttpRequest->extendLayer(0, lengthDifference))
		{
			PCPP_LOG_ERROR("Cannot change layer size");
			return false;
		}
	}
	else if (lengthDifference < 0)
	{
		if (!m_HttpRequest->shortenLayer(0, 0-lengthDifference))
		{
			PCPP_LOG_ERROR("Cannot change layer size");
			return false;

		}
	}

	if (lengthDifference != 0)
		m_HttpRequest->shiftFieldsOffset(m_HttpRequest->getFirstField(), lengthDifference);

	memcpy(m_HttpRequest->m_Data, MethodEnumToString[newMethod].c_str(), MethodEnumToString[newMethod].length());

	m_Method = newMethod;
	m_UriOffset += lengthDifference;
	m_VersionOffset += lengthDifference;

	return true;
}

std::string HttpRequestFirstLine::getUri() const
{
	std::string result;
	if (m_UriOffset != -1 && m_VersionOffset != -1)
		result.assign((const char*)m_HttpRequest->m_Data + m_UriOffset, m_VersionOffset - 6 - m_UriOffset);

	//else first line is illegal, return empty string

	return result;
}

bool HttpRequestFirstLine::setUri(std::string newUri)
{
	// make sure the new URI begins with "/"
	if (newUri.compare(0, 1, "/") != 0)
		newUri = "/" + newUri;

	//extend or shorten layer
	std::string currentUri = getUri();
	int lengthDifference = newUri.length() - currentUri.length();
	if (lengthDifference > 0)
	{
		if (!m_HttpRequest->extendLayer(m_UriOffset, lengthDifference))
		{
			PCPP_LOG_ERROR("Cannot change layer size");
			return false;
		}
	}
	else if (lengthDifference < 0)
	{
		if (!m_HttpRequest->shortenLayer(m_UriOffset, 0-lengthDifference))
		{
			PCPP_LOG_ERROR("Cannot change layer size");
			return false;
		}
	}

	if (lengthDifference != 0)
		m_HttpRequest->shiftFieldsOffset(m_HttpRequest->getFirstField(), lengthDifference);

	memcpy(m_HttpRequest->m_Data + m_UriOffset, newUri.c_str(), newUri.length());

	m_VersionOffset += lengthDifference;

	return true;
}

void HttpRequestFirstLine::setVersion(HttpVersion newVersion)
{
	if (m_VersionOffset == -1)
		return;

	if (newVersion == HttpVersionUnknown)
		return;

	char* verPos = (char*)(m_HttpRequest->m_Data + m_VersionOffset);
	memcpy(verPos, VersionEnumToString[newVersion].c_str(), 3);

	m_Version = newVersion;
}






// -------- Class HttpResponseLayer -----------------



const std::string StatusCodeEnumToString[80] = {
		"Continue",
		"Switching Protocols",
		"Processing",
		"OK",
		"Created",
		"Accepted",
		"Non-Authoritative Information",
		"No Content",
		"Reset Content",
		"Partial Content",
		"Multi-Status",
		"Already Reported",
		"IM Used",
		"Multiple Choices",
		"Moved Permanently",
		"Found",
		"See Other",
		"Not Modified",
		"Use Proxy",
		"Switch Proxy",
		"Temporary Redirect",
		"Permanent Redirect",
		"Bad Request",
		"Unauthorized",
		"Payment Required",
		"Forbidden",
		"Not Found",
		"Method Not Allowed",
		"Not Acceptable",
		"Proxy Authentication Required",
		"Request Timeout",
		"Conflict",
		"Gone",
		"Length Required",
		"Precondition Failed",
		"Request Entity Too Large",
		"Request-URI Too Long",
		"Unsupported Media Type",
		"Requested Range Not Satisfiable",
		"Expectation Failed",
		"I'm a teapot",
		"Authentication Timeout",
		"Method Failure",
		"Unprocessable Entity",
		"Locked",
		"Failed Dependency",
		"Upgrade Required",
		"Precondition Required",
		"Too Many Requests",
		"Request Header Fields Too Large",
		"Login Timeout",
		"No Response",
		"Retry With",
		"Blocked by Windows Parental Controls",
		"Unavailable For Legal Reasons",
		"Request Header Too Large",
		"Cert Error",
		"No Cert",
		"HTTP to HTTPS",
		"Token expired/invalid",
		"Client Closed Request",
		"Internal Server Error",
		"Not Implemented",
		"Bad Gateway",
		"Service Unavailable",
		"Gateway Timeout",
		"HTTP Version Not Supported",
		"Variant Also Negotiates",
		"Insufficient Storage",
		"Loop Detected",
		"Bandwidth Limit Exceeded",
		"Not Extended",
		"Network Authentication Required",
		"Origin Error",
		"Web server is down",
		"Connection timed out",
		"Proxy Declined Request",
		"A timeout occurred",
		"Network read timeout error",
		"Network connect timeout error"
};


const int StatusCodeEnumToInt[80] = {
		100,
		101,
		102,
		200,
		201,
		202,
		203,
		204,
		205,
		206,
		207,
		208,
		226,
		300,
		301,
		302,
		303,
		304,
		305,
		306,
		307,
		308,
		400,
		401,
		402,
		403,
		404,
		405,
		406,
		407,
		408,
		409,
		410,
		411,
		412,
		413,
		414,
		415,
		416,
		417,
		418,
		419,
		420,
		422,
		423,
		424,
		426,
		428,
		429,
		431,
		440,
		444,
		449,
		450,
		451,
		494,
		495,
		496,
		497,
		498,
		499,
		500,
		501,
		502,
		503,
		504,
		505,
		506,
		507,
		508,
		509,
		510,
		511,
		520,
		521,
		522,
		523,
		524,
		598,
		599
};

const std::unordered_map<std::string, HttpResponseStatusCode> StatusCodeStringToEnumMap {
	{"100", HttpResponseStatusCode::Http100Continue },
	{"101", HttpResponseStatusCode::Http101SwitchingProtocols },
	{"102", HttpResponseStatusCode::Http102Processing },
	{"200", HttpResponseStatusCode::Http200OK },
	{"201", HttpResponseStatusCode::Http201Created },
	{"202", HttpResponseStatusCode::Http202Accepted },
	{"203", HttpResponseStatusCode::Http203NonAuthoritativeInformation },
	{"204", HttpResponseStatusCode::Http204NoContent },
	{"205", HttpResponseStatusCode::Http205ResetContent },
	{"206", HttpResponseStatusCode::Http206PartialContent },
	{"207", HttpResponseStatusCode::Http207MultiStatus },
	{"208", HttpResponseStatusCode::Http208AlreadyReported },
	{"226", HttpResponseStatusCode::Http226IMUsed },
	{"300", HttpResponseStatusCode::Http300MultipleChoices },
	{"301", HttpResponseStatusCode::Http301MovedPermanently },
	{"302", HttpResponseStatusCode::Http302 },
	{"303", HttpResponseStatusCode::Http303SeeOther },
	{"304", HttpResponseStatusCode::Http304NotModified },
	{"305", HttpResponseStatusCode::Http305UseProxy },
	{"306", HttpResponseStatusCode::Http306SwitchProxy },
	{"307", HttpResponseStatusCode::Http307TemporaryRedirect },
	{"308", HttpResponseStatusCode::Http308PermanentRedirect },
	{"400", HttpResponseStatusCode::Http400BadRequest },
	{"401", HttpResponseStatusCode::Http401Unauthorized },
	{"402", HttpResponseStatusCode::Http402PaymentRequired },
	{"403", HttpResponseStatusCode::Http403Forbidden },
	{"404", HttpResponseStatusCode::Http404NotFound },
	{"405", HttpResponseStatusCode::Http405MethodNotAllowed },
	{"406", HttpResponseStatusCode::Http406NotAcceptable },
	{"407", HttpResponseStatusCode::Http407ProxyAuthenticationRequired },
	{"408", HttpResponseStatusCode::Http408RequestTimeout },
	{"409", HttpResponseStatusCode::Http409Conflict },
	{"410", HttpResponseStatusCode::Http410Gone },
	{"411", HttpResponseStatusCode::Http411LengthRequired },
	{"412", HttpResponseStatusCode::Http412PreconditionFailed },
	{"413", HttpResponseStatusCode::Http413RequestEntityTooLarge },
	{"414", HttpResponseStatusCode::Http414RequestURITooLong },
	{"415", HttpResponseStatusCode::Http415UnsupportedMediaType },
	{"416", HttpResponseStatusCode::Http416RequestedRangeNotSatisfiable },
	{"417", HttpResponseStatusCode::Http417ExpectationFailed },
	{"418", HttpResponseStatusCode::Http418ImATeapot },
	{"419", HttpResponseStatusCode::Http419AuthenticationTimeout },
	{"420", HttpResponseStatusCode::Http420 },
	{"422", HttpResponseStatusCode::Http422UnprocessableEntity },
	{"423", HttpResponseStatusCode::Http423Locked },
	{"424", HttpResponseStatusCode::Http424FailedDependency },
	{"426", HttpResponseStatusCode::Http426UpgradeRequired },
	{"428", HttpResponseStatusCode::Http428PreconditionRequired },
	{"429", HttpResponseStatusCode::Http429TooManyRequests },
	{"431", HttpResponseStatusCode::Http431RequestHeaderFieldsTooLarge },
	{"440", HttpResponseStatusCode::Http440LoginTimeout },
	{"444", HttpResponseStatusCode::Http444NoResponse },
	{"449", HttpResponseStatusCode::Http449RetryWith },
	{"450", HttpResponseStatusCode::Http450BlockedByWindowsParentalControls },
	{"451", HttpResponseStatusCode::Http451 },
	{"494", HttpResponseStatusCode::Http494RequestHeaderTooLarge },
	{"495", HttpResponseStatusCode::Http495CertError },
	{"496", HttpResponseStatusCode::Http496NoCert },
	{"497", HttpResponseStatusCode::Http497HTTPtoHTTPS },
	{"498", HttpResponseStatusCode::Http498TokenExpiredInvalid },
	{"499", HttpResponseStatusCode::Http499 },
	{"500", HttpResponseStatusCode::Http500InternalServerError },
	{"501", HttpResponseStatusCode::Http501NotImplemented },
	{"502", HttpResponseStatusCode::Http502BadGateway },
	{"503", HttpResponseStatusCode::Http503ServiceUnavailable },
	{"504", HttpResponseStatusCode::Http504GatewayTimeout },
	{"505", HttpResponseStatusCode::Http505HTTPVersionNotSupported },
	{"506", HttpResponseStatusCode::Http506VariantAlsoNegotiates },
	{"507", HttpResponseStatusCode::Http507InsufficientStorage },
	{"508", HttpResponseStatusCode::Http508LoopDetected },
	{"509", HttpResponseStatusCode::Http509BandwidthLimitExceeded },
	{"510", HttpResponseStatusCode::Http510NotExtended },
	{"511", HttpResponseStatusCode::Http511NetworkAuthenticationRequired },
	{"520", HttpResponseStatusCode::Http520OriginError },
	{"521", HttpResponseStatusCode::Http521WebServerIsDown },
	{"522", HttpResponseStatusCode::Http522ConnectionTimedOut },
	{"523", HttpResponseStatusCode::Http523ProxyDeclinedRequest },
	{"524", HttpResponseStatusCode::Http524aTimeoutOccurred },
	{"598", HttpResponseStatusCode::Http598NetworkReadTimeoutError },
	{"599", HttpResponseStatusCode::Http599NetworkConnectTimeoutError }
};



HttpResponseStatusCode::HttpResponseLayer(uint8_t* data, size_t dataLen, Layer* prevLayer, Packet* packet)  : HttpMessage(data, dataLen, prevLayer, packet)
{
	m_Protocol = HTTPResponse;
	m_FirstLine = new HttpResponseFirstLine(this);
	m_FieldsOffset = m_FirstLine->getSize();
	parseFields();
}

HttpResponseStatusCode::HttpResponseLayer(HttpVersion version, HttpResponseStatusCode statusCode, std::string statusCodeString)
{
	m_Protocol = HTTPResponse;
	m_FirstLine = new HttpResponseFirstLine(this, version, statusCode, std::move(statusCodeString));
	m_FieldsOffset = m_FirstLine->getSize();
}

HttpResponseLayer::~HttpResponseLayer()
{
	delete m_FirstLine;
}


HttpResponseStatusCode::HttpResponseLayer(const HttpResponseLayer& other) : HttpMessage(other)
{
	m_FirstLine = new HttpResponseFirstLine(this);
}

HttpResponseLayer& HttpResponseLayer::operator=(const HttpResponseLayer& other)
{
	HttpMessage::operator=(other);

	if (m_FirstLine != nullptr)
		delete m_FirstLine;

	m_FirstLine = new HttpResponseFirstLine(this);

	return *this;
}


HeaderField* HttpResponseLayer::setContentLength(int contentLength, const std::string &prevFieldName)
{
	std::ostringstream contentLengthAsString;
	contentLengthAsString << contentLength;
	std::string contentLengthFieldName(PCPP_HTTP_CONTENT_LENGTH_FIELD);
	HeaderField* contentLengthField = getFieldByName(contentLengthFieldName);
	if (contentLengthField == nullptr)
	{
		HeaderField* prevField = getFieldByName(prevFieldName);
		contentLengthField = insertField(prevField, PCPP_HTTP_CONTENT_LENGTH_FIELD, contentLengthAsString.str());
	}
	else
		contentLengthField->setFieldValue(contentLengthAsString.str());

	return contentLengthField;
}

int HttpResponseLayer::getContentLength() const
{
	std::string contentLengthFieldName(PCPP_HTTP_CONTENT_LENGTH_FIELD);
	std::transform(contentLengthFieldName.begin(), contentLengthFieldName.end(), contentLengthFieldName.begin(), ::tolower);
	HeaderField* contentLengthField = getFieldByName(contentLengthFieldName);
	if (contentLengthField != nullptr)
		return atoi(contentLengthField->getFieldValue().c_str());
	return 0;
}

std::string HttpResponseLayer::toString() const
{
	static const int maxLengthToPrint = 120;
	std::string result = "HTTP response, ";
	int size = m_FirstLine->getSize() - 2; // the -2 is to remove \r\n at the end of the first line
	if (size <= maxLengthToPrint)
	{
		char* firstLine = new char[size+1];
		strncpy(firstLine, (char*)m_Data, size);
		firstLine[size] = 0;
		result += std::string(firstLine);
		delete[] firstLine;
	}
	else
	{
		char firstLine[maxLengthToPrint+1];
		strncpy(firstLine, (char*)m_Data, maxLengthToPrint-3);
		firstLine[maxLengthToPrint-3] = '.';
		firstLine[maxLengthToPrint-2] = '.';
		firstLine[maxLengthToPrint-1] = '.';
		firstLine[maxLengthToPrint] = 0;
		result += std::string(firstLine);
	}

	return result;
}








// -------- Class HttpResponseFirstLine -----------------



int HttpResponseFirstLine::getStatusCodeAsInt() const
{
	return StatusCodeEnumToInt[m_StatusCode];
}

std::string HttpResponseFirstLine::getStatusCodeString() const
{
	std::string result;
	const int statusStringOffset = 13;
	if (m_StatusCode != HttpResponseStatusCode::HttpStatusCodeUnknown)
	{
		int statusStringEndOffset = m_FirstLineEndOffset - 2;
		if ((*(m_HttpResponse->m_Data + statusStringEndOffset)) != '\r')
			statusStringEndOffset++;
		result.assign((char*)(m_HttpResponse->m_Data + statusStringOffset), statusStringEndOffset-statusStringOffset);
	}

	//else first line is illegal, return empty string

	return result;
}

bool HttpResponseFirstLine::setStatusCode(HttpResponseStatusCode newStatusCode, std::string statusCodeString)
{
	if (newStatusCode == HttpResponseStatusCode::HttpStatusCodeUnknown)
	{
		PCPP_LOG_ERROR("Requested status code is HttpStatusCodeUnknown");
		return false;
	}

	//extend or shorten layer

	size_t statusStringOffset = 13;
	if (statusCodeString == "")
		statusCodeString = StatusCodeEnumToString[newStatusCode];
	int lengthDifference = statusCodeString.length() - getStatusCodeString().length();
	if (lengthDifference > 0)
	{
		if (!m_HttpResponse->extendLayer(statusStringOffset, lengthDifference))
		{
			PCPP_LOG_ERROR("Cannot change layer size");
			return false;
		}
	}
	else if (lengthDifference < 0)
	{
		if (!m_HttpResponse->shortenLayer(statusStringOffset, 0-lengthDifference))
		{
			PCPP_LOG_ERROR("Cannot change layer size");
			return false;

		}
	}

	if (lengthDifference != 0)
		m_HttpResponse->shiftFieldsOffset(m_HttpResponse->getFirstField(), lengthDifference);

	// copy status string
	memcpy(m_HttpResponse->m_Data+statusStringOffset, statusCodeString.c_str(), statusCodeString.length());

	// change status code
	std::ostringstream statusCodeAsString;
	statusCodeAsString << StatusCodeEnumToInt[newStatusCode];
	memcpy(m_HttpResponse->m_Data+9, statusCodeAsString.str().c_str(), 3);

	m_StatusCode = newStatusCode;

	m_FirstLineEndOffset += lengthDifference;

	return true;

}

void HttpResponseFirstLine::setVersion(HttpVersion newVersion)
{
	if (newVersion == HttpVersionUnknown)
		return;

	char* verPos = (char*)(m_HttpResponse->m_Data + 5);
	memcpy(verPos, VersionEnumToString[newVersion].c_str(), 3);

	m_Version = newVersion;
}

HttpResponseStatusCode HttpResponseFirstLine::parseStatusCode(const char* data, size_t dataLen)
{
	// minimum data should be 12B long: "HTTP/x.y XXX"
	if (!data || dataLen < 12)
	{
		return HttpResponseStatusCode::HttpStatusCodeUnknown;
	}

	const char* statusCodeData = data + 9;

	auto codeAsEnum = StatusCodeStringToEnumMap.find(std::string(statusCodeData, 3));
	if (codeAsEnum == StatusCodeStringToEnumMap.end())
	{
		return HttpResponseStatusCode::HttpStatusCodeUnknown;
	}
	return codeAsEnum->second;
}

HttpResponseFirstLine::HttpResponseFirstLine(HttpResponseLayer* httpResponse) : m_HttpResponse(httpResponse)
{
	m_Version = parseVersion((char*)m_HttpResponse->m_Data, m_HttpResponse->getDataLen());
	if (m_Version == HttpVersionUnknown)
	{
		m_StatusCode = HttpResponseStatusCode::HttpStatusCodeUnknown;
	}
	else
	{
		m_StatusCode = parseStatusCode((char*)m_HttpResponse->m_Data, m_HttpResponse->getDataLen());
	}


	char* endOfFirstLine;
	if ((endOfFirstLine = (char*)memchr((char*)(m_HttpResponse->m_Data), '\n', m_HttpResponse->m_DataLen)) != nullptr)
	{
		m_FirstLineEndOffset = endOfFirstLine - (char*)m_HttpResponse->m_Data + 1;
		m_IsComplete = true;
	}
	else
	{
		m_FirstLineEndOffset = m_HttpResponse->getDataLen();
		m_IsComplete = false;
	}

	if (Logger::getInstance().isDebugEnabled(PacketLogModuleHttpLayer))
	{
		std::string version = (m_Version == HttpVersionUnknown ? "Unknown" : VersionEnumToString[m_Version]);
		int statusCode = (m_StatusCode == HttpResponseStatusCode::HttpStatusCodeUnknown ? 0 : StatusCodeEnumToInt[m_StatusCode]);
		PCPP_LOG_DEBUG("Version='" << version << "'; Status code=" << statusCode << " '" << getStatusCodeString() << "'");
	}
}


HttpResponseFirstLine::HttpResponseFirstLine(HttpResponseLayer* httpResponse,  HttpVersion version, HttpResponseStatusCode statusCode, std::string statusCodeString)
{
	if (statusCode == HttpResponseStatusCode::HttpStatusCodeUnknown)
	{
		m_Exception.setMessage("Status code supplied was HttpStatusCodeUnknown");
		throw m_Exception;
	}

	if (version == HttpVersionUnknown)
	{
		m_Exception.setMessage("Version supplied was HttpVersionUnknown");
		throw m_Exception;
	}

	m_HttpResponse = httpResponse;

	m_StatusCode = statusCode;
	m_Version = version;

	std::ostringstream statusCodeAsString;
	statusCodeAsString << StatusCodeEnumToInt[m_StatusCode];
	if (statusCodeString == "")
		statusCodeString = StatusCodeEnumToString[m_StatusCode];
	std::string firstLine = "HTTP/" + VersionEnumToString[m_Version] + " " + statusCodeAsString.str() + " " +  statusCodeString +  "\r\n";

	m_FirstLineEndOffset = firstLine.length();

	m_HttpResponse->m_DataLen = firstLine.length();
	m_HttpResponse->m_Data = new uint8_t[m_HttpResponse->m_DataLen];
	memcpy(m_HttpResponse->m_Data, firstLine.c_str(), m_HttpResponse->m_DataLen);

	m_IsComplete = true;
}

HttpVersion HttpResponseFirstLine::parseVersion(const char* data, size_t dataLen)
{
	if (!data || dataLen < 8) // "HTTP/x.y"
	{
		PCPP_LOG_DEBUG("HTTP response length < 8, cannot identify version");
		return HttpVersionUnknown;
	}

	if (data[0] != 'H' || data[1] != 'T' || data[2] != 'T' || data[3] != 'P' || data[4] != '/')
	{
		PCPP_LOG_DEBUG("HTTP response does not begin with 'HTTP/'");
		return HttpVersionUnknown;
	}

	const char* verPos = data + 5;
	auto versionAsEnum = HttpVersionStringToEnum.find(std::string(verPos, verPos + 3));
	if (versionAsEnum == HttpVersionStringToEnum.end())
	{
		return HttpVersionUnknown;
	}
	return versionAsEnum->second;
}

} // namespace pcpp
