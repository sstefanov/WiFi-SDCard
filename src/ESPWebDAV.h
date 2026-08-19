#include <ESP8266WiFi.h>
#include <SdFat.h>

#define DEBUG

#ifdef DEBUG
	#define DBG_PRINT(...) 		{ Serial.print(__VA_ARGS__); }
	#define DBG_PRINTLN(...) 	{ Serial.println(__VA_ARGS__); }
#else
	#define DBG_PRINT(...) 		{}
	#define DBG_PRINTLN(...) 	{}
#endif

// constants for WebServer
#define CONTENT_LENGTH_UNKNOWN -1
#define CONTENT_LENGTH_NOT_SET -2
#define HTTP_MAX_POST_WAIT 		30000 
// cap simultaneously accepted/pending TCP clients (1 active + this many queued)
#define MAX_PENDING_CLIENTS 	1

enum ResourceType { RESOURCE_NONE, RESOURCE_FILE, RESOURCE_DIR };
enum DepthType { DEPTH_NONE, DEPTH_CHILD, DEPTH_ALL };


class ESPWebDAV	{
public:
	bool init(int chipSelectPin, SPISettings spiSettings, int serverPort);
  bool initSD(int chipSelectPin, SPISettings spiSettings);
	bool ensureServer(int serverPort);
  bool startServer();
	bool isServerReady();
	bool isClientWaiting();
	void handleClient(String blank = "");
    void handleHttpClient(String blank = "");
    void handleWebDAVClient(String blank = "");
    void rejectClient(String rejectMessage);
    String readLine();
    int cardMounted();
    SdFat& fileSystem() { return sd; }

  protected:
	typedef void (ESPWebDAV::*THandlerFunction)(String);
	
	void processClient(THandlerFunction handler, String message);
	void handleNotFound();
	void handleReject(String rejectMessage);
	void handleRequest(String blank);
    void handleWEBDAV(String blank);
    void handleOptions(ResourceType resource);
    void handleLock(ResourceType resource);
	void handleUnlock(ResourceType resource);
	void handlePropPatch(ResourceType resource);
	void handleProp(ResourceType resource);
	void sendPropResponse(boolean recursing, FatFile *curFile);
	void handleGet(ResourceType resource, bool isGet);
	void handlePut(ResourceType resource);
	bool uploadFileData(const char* filePath, size_t contentLength);
	void handleWriteError(String message, FatFile *wFile);
	void handleDirectoryCreate(ResourceType resource);
	void handleMove(ResourceType resource);
	void handleDelete(ResourceType resource);
    // http methods
    bool checkCardBusOrReject();
    void handleHttp(THandlerFunction handler, String message);
    void handleFileList(String message);
    void handleFileDownload(String message);
    void handleFileUpload(String message);
	void handleFileDelete(String message);
    void handleStatusPage(String message);
	void handleCardStatus(String message);
	void handleSettingsPage(String message);

    // Sections are copied from ESP8266Webserver
	String getMimeType(String path);
	String urlDecode(const String& text);
	String urlToUri(String url);
	bool parseRequest();
	void sendHeader(const String& name, const String& value, bool first = false);
	void send(String code, const char* content_type, const String& content);
	void _prepareHeader(String& response, String code, const char* content_type, size_t contentLength);
	void sendContent(const String& content);
	void sendContent_P(PGM_P content);
	void setContentLength(size_t len);
	size_t readBytesWithTimeout(uint8_t *buf, size_t bufSize);
	size_t readBytesWithTimeout(uint8_t *buf, size_t bufSize, size_t numToRead);
	
	
	// variables pertaining to current most HTTP request being serviced
	WiFiServer *server = nullptr;
	SdFat sd;

	WiFiClient 	client;
	String 		method;
	String 		uri;
	String 		contentLengthHeader;
    String 		contentTypeHeader;
    String 		depthHeader;
    String 		hostHeader;
	String		destinationHeader;

	String 		_responseHeaders;
	bool		_chunked;
	int			_contentLength;
};

extern ESPWebDAV dav;
