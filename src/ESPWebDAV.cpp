// WebDAV server using ESP8266 and SD card filesystem
// Targeting Windows 7 Explorer WebDav

#include "ESPWebDAV.h"
#include "network.h"
#include "config.h"
#include "sdControl.h"
#include <ESP8266WiFi.h>
#include <Hash.h>
#include <SPI.h>
#include <SdFat.h>
#include <time.h>
#include <ctype.h>
#include <new>
#include "pins.h"
#include "pathUtils.h"
#include "sdFileUtils.h"

const char *months[]  = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *wdays[]  = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

class StringPrint : public Print {
public:
    explicit StringPrint(String &dst) : _dst(dst) {}

    size_t write(uint8_t c) override {
        _dst += (char)c;
        return 1;
    }

    size_t write(const uint8_t *buffer, size_t size) override {
        for (size_t i = 0; i < size; i++) {
            _dst += (char)buffer[i];
        }
        return size;
    }

private:
    String &_dst;
};


// ------------------------
bool ESPWebDAV::init(int chipSelectPin, SPISettings spiSettings, int serverPort) {
// ------------------------
    // start the wifi server
    DBG_PRINTLN("DAV::init");
    if (!ensureServer(serverPort)) {
        return false;
    }

// initialize the SD card
return sd.begin(chipSelectPin, spiSettings);
}

// ------------------------
bool ESPWebDAV::ensureServer(int serverPort) {
// ------------------------
    if (!server)
        server = new WiFiServer(serverPort);

    if (!server)
        return false;

    server->begin(serverPort, MAX_PENDING_CLIENTS);
    return true;
}

// ------------------------
bool ESPWebDAV::initSD(int chipSelectPin, SPISettings spiSettings) {
	// initialize the SD card
	return sd.begin(chipSelectPin, spiSettings);
}

// ------------------------
bool ESPWebDAV::startServer() {
// ------------------------
    // start the wifi server
    if (!server)
        return false;
    server->begin(server->port(), MAX_PENDING_CLIENTS);
    return true;
}

// ------------------------
void ESPWebDAV::handleNotFound() {
// ------------------------
	String message = "Not found\n";
	message += "URI: ";
	message += uri;
	message += " Method: ";
	message += method;
	message += "\n";

	sendHeader("Allow", "OPTIONS,MKCOL,POST,PUT");
	send("404 Not Found", "text/plain", message);
	DBG_PRINTLN("404 Not Found");
}



// ------------------------
void ESPWebDAV::handleReject(String rejectMessage)	{
// ------------------------
	DBG_PRINT("Rejecting request: "); DBG_PRINTLN(rejectMessage);

	// handle options
	if(method.equals("OPTIONS"))
		return handleOptions(RESOURCE_NONE);
	
	// handle properties
	if(method.equals("PROPFIND"))	{
		sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE");
		setContentLength(CONTENT_LENGTH_UNKNOWN);
		send("207 Multi-Status", "application/xml;charset=utf-8", "");
		sendContent(F("<?xml version=\"1.0\" encoding=\"utf-8\"?><D:multistatus xmlns:D=\"DAV:\"><D:response><D:href>/</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>Fri, 30 Nov 1979 00:00:00 GMT</D:getlastmodified><D:getetag>\"3333333333333333333333333333333333333333\"</D:getetag><D:resourcetype><D:collection/></D:resourcetype></D:prop></D:propstat></D:response>"));
		
		if(depthHeader.equals("1"))	{
			sendContent(F("<D:response><D:href>/"));
			sendContent(rejectMessage);
			sendContent(F("</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>Fri, 01 Apr 2016 16:07:40 GMT</D:getlastmodified><D:getetag>\"2222222222222222222222222222222222222222\"</D:getetag><D:resourcetype/><D:getcontentlength>0</D:getcontentlength><D:getcontenttype>application/octet-stream</D:getcontenttype></D:prop></D:propstat></D:response>"));
		}
		
		sendContent(F("</D:multistatus>"));
		return;
	}
	else
		// if reached here, means its a 404
		handleNotFound();
}




// set http_proxy=http://localhost:36036
// curl -v -X PROPFIND -H "Depth: 1" http://Rigidbot/Old/PipeClip.gcode
// Test PUT a file: curl -v -T c.txt -H "Expect:" http://Rigidbot/c.txt
// C:\Users\gsbal>curl -v -X LOCK http://Rigidbot/EMA_CPP_TRCC_Tutorial/Consumer.cpp -d "<?xml version=\"1.0\" encoding=\"utf-8\" ?><D:lockinfo xmlns:D=\"DAV:\"><D:lockscope><D:exclusive/></D:lockscope><D:locktype><D:write/></D:locktype><D:owner><D:href>CARBON2\gsbal</D:href></D:owner></D:lockinfo>"

// ------------------------
void ESPWebDAV::handleRequest(String blank) {
// ------------------------
    String route = urlToUri(uri);
    route.trim();
    if (route.length() == 0) {
        route = "/";
    }
    if (!route.startsWith("/")) {
        route = "/" + route;
    }

    int queryPos = route.indexOf('?');
    if (queryPos == 0) {
        route = "/";
    } else if (queryPos > 0) {
        route = route.substring(0, queryPos);
    }
    if (route.length() == 0) {
        route = "/";
    }

    bool quietCardStatus = route.startsWith("/cardstatus");
    if (!quietCardStatus) {
        DBG_PRINT("uri: ");
        DBG_PRINTLN(uri);
        DBG_PRINT("method: ");
        DBG_PRINTLN(method);
        DBG_PRINT("route: ");
        DBG_PRINTLN(route);
    }

    if (route == "/") {
        if (method == "GET") {
            // Browser request → file list page
            handleHttp(&ESPWebDAV::handleFileList, blank);
        } else {
            // Any other method → WebDAV
            handleWEBDAV(blank);
        }
    }
    else if (route.equalsIgnoreCase("/favicon.ico")) {
        // Dummy empty favicon
        send("200 OK", "image/x-icon", "");
    }
    else if (route.equalsIgnoreCase("/robots.txt")) {
        // Allow everything
        send("200 OK", "text/plain", "User-agent: *\nDisallow:");
    } else if (route.startsWith("/files")) {
        handleHttp(&ESPWebDAV::handleFileList, blank);
    } else if (route.startsWith("/download")) {
        handleHttp(&ESPWebDAV::handleFileDownload, blank);
    } else if (route.startsWith("/upload")) {
        handleHttp(&ESPWebDAV::handleFileUpload, blank);
    } else if (route.startsWith("/delete")) {
        handleHttp(&ESPWebDAV::handleFileDelete, blank);
    } else if (route.startsWith("/status")) {
        handleStatusPage(blank);
    } else if (route.startsWith("/settings")) {
        handleSettingsPage(blank);
    } else if (route.startsWith("/cardstatus")) {
        handleCardStatus(blank);
    } else {
        // Everything else → WebDAV
        handleWEBDAV(blank);
    }
}

void ESPWebDAV::handleHttpClient(String blank) {
    processClient(&ESPWebDAV::handleRequest, blank);
}

void ESPWebDAV::handleWebDAVClient(String blank) {
    processClient(&ESPWebDAV::handleWEBDAV, blank);
}

// ------------------------
void ESPWebDAV::handleWEBDAV(String blank) {
// ------------------------
    DBG_PRINT("handleWEBDAV");
    if (!network.ready()) {
        rejectClient("Failed to initialize SD Card");
        return;
    }

        // has other master been using the bus in last few seconds
        if (!sdcontrol.canWeTakeBus()) {
            rejectClient("Marlin is reading from SD card");
            return;
        }
        sdcontrol.takeBusControl();
        ResourceType resource = RESOURCE_NONE;

        // does uri refer to a file or directory or a null?
        SdFile tFile;
        if (SdFileUtils::openDirectory(sd, tFile, uri)) {
            resource = RESOURCE_DIR;
            tFile.close();
        } else if (SdFileUtils::openFileRead(sd, tFile, uri)) {
            resource = RESOURCE_FILE;
            tFile.close();
        }

        DBG_PRINT("\r\nm: ");
        DBG_PRINT(method);
        DBG_PRINT(" r: ");
        DBG_PRINT(resource);
        DBG_PRINT(" u: ");
        DBG_PRINTLN(uri);

        // add header that gets sent everytime
        sendHeader("DAV", "2");

        // handle properties
        if (method.equals("PROPFIND"))
            return handleProp(resource);

        if (method.equals("GET"))
            return handleGet(resource, true);

        if (method.equals("HEAD"))
            return handleGet(resource, false);

        // handle options
        if (method.equals("OPTIONS"))
            return handleOptions(resource);

        // handle file create/uploads
        if (method.equals("PUT"))
            return handlePut(resource);

        // handle file locks
        if (method.equals("LOCK"))
            return handleLock(resource);

        if (method.equals("UNLOCK"))
            return handleUnlock(resource);

        if (method.equals("PROPPATCH"))
            return handlePropPatch(resource);

        // directory creation
        if (method.equals("MKCOL"))
            return handleDirectoryCreate(resource);

        // move a file or directory
        if (method.equals("MOVE"))
            return handleMove(resource);

        // delete a file or directory
        if (method.equals("DELETE"))
            return handleDelete(resource);

        // if reached here, means its a 404
        handleNotFound();
        sdcontrol.relinquishBusControl();
    }

    // ------------------------
    // void ESPWebDAV::handleOptions(ResourceType resource) {
    //     // ------------------------
    //     DBG_PRINTLN("Processing OPTION");
    //     sendHeader("Allow", "PROPFIND,GET,DELETE,PUT,COPY,MOVE");
    //     send("200 OK", NULL, "");
    // }
    void ESPWebDAV::handleOptions(ResourceType resource) {
        DBG_PRINTLN("Processing OPTIONS");

        // Required headers for WebDAV clients
        sendHeader("Allow", "OPTIONS, GET, HEAD, POST, PUT, DELETE, PROPFIND, MKCOL, MOVE, COPY, LOCK, UNLOCK");
        sendHeader("DAV", "1,2");
        sendHeader("Content-Length", "0");

        // Finalize response with empty body
        send("200 OK", nullptr, "");

        // Release SD card bus if it was taken
        sdcontrol.relinquishBusControl();
    }

    // ------------------------
    void ESPWebDAV::handleLock(ResourceType resource) {
        // ------------------------
        DBG_PRINTLN("Processing LOCK");

        // does URI refer to an existing resource
        if (resource == RESOURCE_NONE)
            return handleNotFound();

        sendHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
        sendHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");

        size_t contentLen = contentLengthHeader.toInt();
        uint8_t buf[1024];
        size_t numRead = readBytesWithTimeout(buf, sizeof(buf), contentLen);

        if (numRead == 0)
            return handleNotFound();

        buf[contentLen] = 0;
        String inXML = String((char *)buf);
        int startIdx = inXML.indexOf("<D:href>");
        int endIdx = inXML.indexOf("</D:href>");
        if (startIdx < 0 || endIdx < 0)
            return handleNotFound();

        String lockUser = inXML.substring(startIdx + 8, endIdx);
        String resp1 = F("<?xml version=\"1.0\" encoding=\"utf-8\"?><D:prop xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock><D:locktype><write/></D:locktype><D:lockscope><exclusive/></D:lockscope><D:locktoken><D:href>urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9</D:href></D:locktoken><D:lockroot><D:href>");
        String resp2 = F("</D:href></D:lockroot><D:depth>infinity</D:depth><D:owner><a:href xmlns:a=\"DAV:\">");
        String resp3 = F("</a:href></D:owner><D:timeout>Second-3600</D:timeout></D:activelock></D:lockdiscovery></D:prop>");

        send("200 OK", "application/xml;charset=utf-8", resp1 + uri + resp2 + lockUser + resp3);
    }

    // ------------------------
    void ESPWebDAV::handleUnlock(ResourceType resource) {
        // ------------------------
        DBG_PRINTLN("Processing UNLOCK");
        sendHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
        sendHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");
        send("204 No Content", NULL, "");
    }

    // ------------------------
    void ESPWebDAV::handlePropPatch(ResourceType resource) {
        // ------------------------
        DBG_PRINTLN("PROPPATCH forwarding to PROPFIND");
        handleProp(resource);
    }

    // ------------------------
    void ESPWebDAV::handleProp(ResourceType resource) {
        // ------------------------
        DBG_PRINTLN("Processing PROPFIND");
        // check depth header
        DepthType depth = DEPTH_NONE;
        if (depthHeader.equals("1"))
            depth = DEPTH_CHILD;
        else if (depthHeader.equals("infinity"))
            depth = DEPTH_ALL;

        DBG_PRINT("Depth: ");
        DBG_PRINTLN(depth);

        // does URI refer to an existing resource
        if (resource == RESOURCE_NONE)
            return handleNotFound();

        if (resource == RESOURCE_FILE)
            sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
        else
            sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,PUT,MKCOL");

        setContentLength(CONTENT_LENGTH_UNKNOWN);
        send("207 Multi-Status", "application/xml;charset=utf-8", "");
        sendContent(F("<?xml version=\"1.0\" encoding=\"utf-8\"?>"));
        sendContent(F("<D:multistatus xmlns:D=\"DAV:\">"));

        // open this resource
        SdFile baseFile;
        baseFile.open(uri.c_str(), O_READ);
        sendPropResponse(false, &baseFile);

        if ((resource == RESOURCE_DIR) && (depth == DEPTH_CHILD)) {
            // append children information to message
            SdFile childFile;
            while (childFile.openNext(&baseFile, O_READ)) {
                yield();
                sendPropResponse(true, &childFile);
                childFile.close();
            }
        }

        baseFile.close();
        sendContent(F("</D:multistatus>"));
    }

    // ------------------------
    void ESPWebDAV::sendPropResponse(boolean recursing, FatFile * curFile) {
        // ------------------------
        char buf[255];
        curFile->getName(buf, sizeof(buf));

        // String fullResPath = "http://" + hostHeader + uri;
        String fullResPath = uri;

        if (recursing) {
            if (fullResPath.endsWith("/"))
                fullResPath += String(buf);
            else
                fullResPath += "/" + String(buf);
        }
        DBG_PRINT("fullResPath: ");
        DBG_PRINTLN(fullResPath);

        // get file modified time
        dir_t dir;
        curFile->dirEntry(&dir);

        // convert to required format
        tm tmStr;
        tmStr.tm_hour = FAT_HOUR(dir.lastWriteTime);
        tmStr.tm_min = FAT_MINUTE(dir.lastWriteTime);
        tmStr.tm_sec = FAT_SECOND(dir.lastWriteTime);
        tmStr.tm_year = FAT_YEAR(dir.lastWriteDate) - 1900;
        tmStr.tm_mon = FAT_MONTH(dir.lastWriteDate) - 1;
        tmStr.tm_mday = FAT_DAY(dir.lastWriteDate);
        time_t t2t = mktime(&tmStr);
        tm *gTm = gmtime(&t2t);

        // Tue, 13 Oct 2015 17:07:35 GMT
        sprintf(buf, "%s, %02d %s %04d %02d:%02d:%02d GMT", wdays[gTm->tm_wday], gTm->tm_mday, months[gTm->tm_mon], gTm->tm_year + 1900, gTm->tm_hour, gTm->tm_min, gTm->tm_sec);
        String fileTimeStamp = String(buf);

        // send the XML information about thyself to client
        sendContent(F("<D:response><D:href>"));
        // append full file path
        sendContent(fullResPath);
        sendContent(F("</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>"));
        // append modified date
        sendContent(fileTimeStamp);
        sendContent(F("</D:getlastmodified><D:getetag>"));
        // append unique tag generated from full path
        sendContent("\"" + sha1(fullResPath + fileTimeStamp) + "\"");
        sendContent(F("</D:getetag>"));

        if (curFile->isDir())
            sendContent(F("<D:resourcetype><D:collection/></D:resourcetype>"));
        else {
            sendContent(F("<D:resourcetype/><D:getcontentlength>"));
            // append the file size
            sendContent(String(curFile->fileSize()));
            sendContent(F("</D:getcontentlength><D:getcontenttype>"));
            // append correct file mime type
            sendContent(getMimeType(fullResPath));
            sendContent(F("</D:getcontenttype>"));
        }
        sendContent(F("</D:prop></D:propstat></D:response>"));
    }

    // ------------------------
    void ESPWebDAV::handleGet(ResourceType resource, bool isGet) {
        // ------------------------
        DBG_PRINTLN("Processing GET");

        // does URI refer to an existing file resource
        if (resource != RESOURCE_FILE)
            return handleNotFound();

        SdFile rFile;
        long tStart = millis();
        uint8_t buf[1460];
        if (!SdFileUtils::openFileRead(sd, rFile, uri)) {
            return handleNotFound();
        }

        sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
        size_t fileSize = rFile.fileSize();
        setContentLength(fileSize);
        String contentType = getMimeType(uri);
        if (uri.endsWith(".gz") && contentType != "application/x-gzip" && contentType != "application/octet-stream")
            sendHeader("Content-Encoding", "gzip");

        send("200 OK", contentType.c_str(), "");

        if (isGet) {
            // disable Nagle if buffer size > TCP MTU of 1460
            // client.setNoDelay(1);

            // send the file
            while (rFile.available()) {
                // SD read speed ~ 17sec for 4.5MB file
                int numRead = rFile.read(buf, sizeof(buf));
                client.write(buf, numRead);
            }
        }

        rFile.close();
        DBG_PRINT("File ");
        DBG_PRINT(fileSize);
        DBG_PRINT(" bytes sent in: ");
        DBG_PRINT((millis() - tStart) / 1000);
        DBG_PRINTLN(" sec");
    }

    // ------------------------
    // ------------------------
    bool ESPWebDAV::uploadFileData(const char* filePath, size_t contentLength) {
        // ------------------------
        // Shared upload implementation used by both WebDAV PUT and HTTP upload
        DBG_PRINT("[DAV] Uploading file: ");
        DBG_PRINTLN(filePath);
        DBG_PRINT("[DAV] Content length: ");
        DBG_PRINTLN(contentLength);

        const size_t WRITE_BLOCK_SIZE = 512;
        uint8_t buf[WRITE_BLOCK_SIZE];
        size_t numRemaining = contentLength;
        size_t bytesWritten = 0;
        long tStart = millis();
        unsigned long lastYield = millis();

        if (contentLength == 0) {
            SdFile emptyFile;
            if (!emptyFile.open(sd.vwd(), filePath, O_WRITE | O_CREAT | O_TRUNC)) {
                DBG_PRINTLN("[DAV] open empty file failed");
                return false;
            }
            bool ok = emptyFile.sync();
            emptyFile.close();
            return ok;
        }

        sd.remove(filePath);

        size_t contBlocks = (contentLength + WRITE_BLOCK_SIZE - 1) / WRITE_BLOCK_SIZE;
        uint32_t bgnBlock, endBlock;

        SdFile file;
        if (!file.createContiguous(sd.vwd(), filePath, contentLength)) {
            DBG_PRINTLN("[DAV] createContiguous failed");
            return false;
        }

        if (!file.contiguousRange(&bgnBlock, &endBlock)) {
            DBG_PRINTLN("[DAV] contiguousRange failed");
            file.close();
            sd.remove(filePath);
            return false;
        }

        if (!sd.card()->writeStart(bgnBlock, contBlocks)) {
            DBG_PRINTLN("[DAV] writeStart failed");
            file.close();
            sd.remove(filePath);
            return false;
        }

        while (numRemaining > 0) {
            if (millis() - lastYield > 50) {
                yield();
                ESP.wdtFeed();
                lastYield = millis();
            }

            size_t numToRead = (numRemaining > WRITE_BLOCK_SIZE) ? WRITE_BLOCK_SIZE : numRemaining;
            if (numToRead < WRITE_BLOCK_SIZE) {
                memset(buf, 0, sizeof(buf));
            }
            size_t numRead = readBytesWithTimeout(buf, sizeof(buf), numToRead);

            if (numRead == 0) {
                DBG_PRINTLN("[DAV] read timeout");
                sd.card()->writeStop();
                file.close();
                sd.remove(filePath);
                return false;
            }

            if (!sd.card()->writeData(buf)) {
                DBG_PRINTLN("[DAV] writeData failed");
                sd.card()->writeStop();
                file.close();
                sd.remove(filePath);
                return false;
            }

            numRemaining -= numRead;
            bytesWritten += numRead;

            if ((bytesWritten % 102400) == 0 || numRemaining == 0) {
                DBG_PRINT("[DAV] Progress: ");
                DBG_PRINT(bytesWritten);
                DBG_PRINT("/");
                DBG_PRINTLN(contentLength);
            }
        }

        if (!sd.card()->writeStop()) {
            DBG_PRINTLN("[DAV] writeStop failed");
            file.close();
            sd.remove(filePath);
            return false;
        }

        file.close();

        DBG_PRINT("[DAV] Upload complete: ");
        DBG_PRINT(bytesWritten);
        DBG_PRINT(" bytes in ");
        DBG_PRINT((millis() - tStart) / 1000);
        DBG_PRINTLN(" sec");

        return true;
    }

    // ------------------------
    void ESPWebDAV::handlePut(ResourceType resource) {
        // ------------------------
        DBG_PRINTLN("Processing Put");

        // does URI refer to a directory
        if (resource == RESOURCE_DIR)
            return handleNotFound();

        sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
        
        size_t contentLen = contentLengthHeader.toInt();
        
        // Use shared upload function
        if (contentLen > 0) {
            if (!uploadFileData(uri.c_str(), contentLen)) {
                send("500 Internal Server Error", "text/plain", "Upload failed");
                return;
            }
        }

        if (resource == RESOURCE_NONE)
            send("201 Created", NULL, "");
        else
            send("200 OK", NULL, "");
    }

    // ------------------------
    void ESPWebDAV::handleWriteError(String message, FatFile * wFile) {
        // ------------------------
        // close this file
        wFile->close();
        // delete the wrile being written
        SdFileUtils::removeFile(sd, uri);
        // send error
        send("500 Internal Server Error", "text/plain", message);
        DBG_PRINTLN(message);
    }

    // ------------------------
    void ESPWebDAV::handleDirectoryCreate(ResourceType resource) {
        // ------------------------
        DBG_PRINTLN("Processing MKCOL");

        // does URI refer to anything
        if (resource != RESOURCE_NONE)
            return handleNotFound();

        // create directory
        if (!sd.mkdir(uri.c_str(), true)) {
            // send error
            send("500 Internal Server Error", "text/plain", "Unable to create directory");
            DBG_PRINTLN("Unable to create directory");
            return;
        }

        DBG_PRINT(uri);
        DBG_PRINTLN(" directory created");
        sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
        send("201 Created", NULL, "");
    }

    // ------------------------
    void ESPWebDAV::handleMove(ResourceType resource) {
        // ------------------------
        DBG_PRINTLN("Processing MOVE");

        // does URI refer to anything
        if (resource == RESOURCE_NONE)
            return handleNotFound();

        if (destinationHeader.length() == 0)
            return handleNotFound();

        String dest = urlToUri(destinationHeader);

        DBG_PRINT("Move destination: ");
        DBG_PRINTLN(dest);

        // move file or directory
        if (!sd.rename(uri.c_str(), dest.c_str())) {
            // send error
            send("500 Internal Server Error", "text/plain", "Unable to move");
            DBG_PRINTLN("Unable to move file/directory");
            return;
        }

        DBG_PRINTLN("Move successful");
        sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
        send("201 Created", NULL, "");
    }

    // ------------------------
    void ESPWebDAV::handleDelete(ResourceType resource) {
        // ------------------------
        DBG_PRINTLN("Processing DELETE");

        // does URI refer to anything
        if (resource == RESOURCE_NONE)
            return handleNotFound();

        bool retVal;

        if (resource == RESOURCE_FILE)
            // delete a file
            retVal = SdFileUtils::removeFile(sd, uri);
        else
            // delete a directory
            retVal = sd.rmdir(uri.c_str());

        if (!retVal) {
            // send error
            send("500 Internal Server Error", "text/plain", "Unable to delete");
            DBG_PRINTLN("Unable to delete file/directory");
            return;
        }

        DBG_PRINTLN("Delete successful");
        sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
        send("200 OK", NULL, "");
    }

    // ------------------------
    String ESPWebDAV::getMimeType(String path) {
        // ------------------------
        if (path.endsWith(".html"))
            return "text/html";
        else if (path.endsWith(".htm"))
            return "text/html";
        else if (path.endsWith(".css"))
            return "text/css";
        else if (path.endsWith(".txt"))
            return "text/plain";
        else if (path.endsWith(".js"))
            return "application/javascript";
        else if (path.endsWith(".json"))
            return "application/json";
        else if (path.endsWith(".png"))
            return "image/png";
        else if (path.endsWith(".gif"))
            return "image/gif";
        else if (path.endsWith(".jpg"))
            return "image/jpeg";
        else if (path.endsWith(".ico"))
            return "image/x-icon";
        else if (path.endsWith(".svg"))
            return "image/svg+xml";
        else if (path.endsWith(".ttf"))
            return "application/x-font-ttf";
        else if (path.endsWith(".otf"))
            return "application/x-font-opentype";
        else if (path.endsWith(".woff"))
            return "application/font-woff";
        else if (path.endsWith(".woff2"))
            return "application/font-woff2";
        else if (path.endsWith(".eot"))
            return "application/vnd.ms-fontobject";
        else if (path.endsWith(".sfnt"))
            return "application/font-sfnt";
        else if (path.endsWith(".xml"))
            return "text/xml";
        else if (path.endsWith(".pdf"))
            return "application/pdf";
        else if (path.endsWith(".zip"))
            return "application/zip";
        else if (path.endsWith(".gz"))
            return "application/x-gzip";
        else if (path.endsWith(".appcache"))
            return "text/cache-manifest";

        return "application/octet-stream";
    }

    // ------------------------
    String ESPWebDAV::urlDecode(const String &text) {
        // ------------------------
        String decoded = "";
        char temp[] = "0x00";
        unsigned int len = text.length();
        unsigned int i = 0;
        while (i < len) {
            char decodedChar;
            char encodedChar = text.charAt(i++);
            if ((encodedChar == '%') && (i + 1 < len)) {
                temp[2] = text.charAt(i++);
                temp[3] = text.charAt(i++);
                decodedChar = strtol(temp, NULL, 16);
            } else {
                if (encodedChar == '+')
                    decodedChar = ' ';
                else
                    decodedChar = encodedChar; // normal ascii char
            }
            decoded += decodedChar;
        }
        return decoded;
    }

    // ------------------------
    String ESPWebDAV::urlToUri(String url) {
        // ------------------------
        if (url.startsWith("http://")) {
            int uriStart = url.indexOf('/', 7);
            return url.substring(uriStart);
        } else
            return url;
    }

    // ------------------------
    bool ESPWebDAV::checkCardBusOrReject() {
        // ------------------------
        if (sdcontrol.canWeTakeBus())
            return true;

        send("503 Service Unavailable", "text/html",
             "<!DOCTYPE html><html><body><p style='color:red'>&#9888; The printer is using SD card.</p></body></html>");
        return false;
    }

    // ------------------------
    bool ESPWebDAV::isServerReady() {
        // ------------------------
        return server != nullptr;
    }

    // ------------------------
    bool ESPWebDAV::isClientWaiting() {
        // ------------------------
        return server && server->hasClient();
    }

    // ------------------------
    void ESPWebDAV::handleClient(String blank) {
        // ------------------------
        DBG_PRINTLN("DAV::handleClient");
        processClient(&ESPWebDAV::handleRequest, blank);
    }

    // ------------------------
    void ESPWebDAV::rejectClient(String rejectMessage) {
        // ------------------------
        processClient(&ESPWebDAV::handleReject, rejectMessage);
    }

    // ------------------------
    void ESPWebDAV::processClient(THandlerFunction handler, String message) {
        // DBG_PRINTLN("DAV::processClient");
        // ------------------------
        // Check if a client has connected
        if (!server) {
            return;
        }

        client = server->accept();

        if (!client)
            return;
        // DBG_PRINTLN("DAV::processClient Server OK");
        // Wait until the client sends some data
        while (!client.available())
            delay(1);

        // reset all variables
        _chunked = false;
        _responseHeaders = String();
        _contentLength = CONTENT_LENGTH_NOT_SET;
        method = String();
        uri = String();
        contentLengthHeader = String();
        contentTypeHeader = String();
        depthHeader = String();
        hostHeader = String();
        destinationHeader = String();

        // extract uri, headers etc
        if (parseRequest())
            // invoke the handler
            (this->*handler)(message);

        // finalize the response
        if (_chunked)
            sendContent("");

        // send all data before closing connection
        client.flush();
        // close the connection
        client.stop();
    }

    // ------------------------
    bool ESPWebDAV::parseRequest() {
        // ------------------------
        // Read the first line of HTTP request
        String req = client.readStringUntil('\r');
        client.readStringUntil('\n');

        // First line of HTTP request looks like "GET /path HTTP/1.1"
        // Retrieve the "/path" part by finding the spaces
        int addr_start = req.indexOf(' ');
        int addr_end = req.indexOf(' ', addr_start + 1);
        if (addr_start == -1 || addr_end == -1) {
            return false;
        }

        method = req.substring(0, addr_start);
        uri = urlDecode(req.substring(addr_start + 1, addr_end));
        bool quietCardStatus = uri.startsWith("/cardstatus");
        if (!quietCardStatus) {
            DBG_PRINT("method: ");
            DBG_PRINT(method);
            DBG_PRINT(" url: ");
            DBG_PRINTLN(uri);
        }

        // parse and finish all headers
        String headerName;
        String headerValue;

        while (1) {
            req = client.readStringUntil('\r');
            client.readStringUntil('\n');
            if (req == "")
                // no more headers
                break;

            int headerDiv = req.indexOf(':');
            if (headerDiv == -1)
                break;

            headerName = req.substring(0, headerDiv);
            headerValue = req.substring(headerDiv + 2);
            if (!quietCardStatus) {
                DBG_PRINT("\t");
                DBG_PRINT(headerName);
                DBG_PRINT(": ");
                DBG_PRINTLN(headerValue);
            }

            if (headerName.equalsIgnoreCase("Host"))
                hostHeader = headerValue;
            else if (headerName.equalsIgnoreCase("Depth"))
                depthHeader = headerValue;
            else if (headerName.equalsIgnoreCase("Content-Length"))
                contentLengthHeader = headerValue;
            else if (headerName.equalsIgnoreCase("Destination"))
                destinationHeader = headerValue;
            else if (headerName.equalsIgnoreCase("Content-Type"))
                contentTypeHeader = headerValue;
        }

        return true;
    }

    // ------------------------
    void ESPWebDAV::sendHeader(const String &name, const String &value, bool first) {
        // ------------------------
        String headerLine = name + ": " + value + "\r\n";

        if (first)
            _responseHeaders = headerLine + _responseHeaders;
        else
            _responseHeaders += headerLine;
    }

    // ------------------------
    void ESPWebDAV::send(String code, const char *content_type, const String &content) {
        // ------------------------
        String header;
        _prepareHeader(header, code, content_type, content.length());

        client.write(header.c_str(), header.length());
        if (content.length())
            sendContent(content);
    }

    // ------------------------
    void ESPWebDAV::_prepareHeader(String &response, String code, const char *content_type, size_t contentLength) {
        // ------------------------
        response = "HTTP/1.1 " + code + "\r\n";

        if (content_type)
            sendHeader("Content-Type", content_type, true);

        if (_contentLength == CONTENT_LENGTH_NOT_SET)
            sendHeader("Content-Length", String(contentLength));
        else if (_contentLength != CONTENT_LENGTH_UNKNOWN)
            sendHeader("Content-Length", String(_contentLength));
        else if (_contentLength == CONTENT_LENGTH_UNKNOWN) {
            _chunked = true;
            sendHeader("Accept-Ranges", "none");
            sendHeader("Transfer-Encoding", "chunked");
        }
        sendHeader("Connection", "close");

        response += _responseHeaders;
        response += "\r\n";
    }

    // ------------------------
    void ESPWebDAV::sendContent(const String &content) {
        // ------------------------
        const char *footer = "\r\n";
        size_t size = content.length();

        if (_chunked) {
            char *chunkSize = (char *)malloc(11);
            if (chunkSize) {
                sprintf(chunkSize, "%x%s", size, footer);
                client.write(chunkSize, strlen(chunkSize));
                free(chunkSize);
            }
        }

        client.write(content.c_str(), size);
        // DBG_PRINT("content: ");
        // DBG_PRINTLN(content.c_str());

        if (_chunked) {
            client.write(footer, 2);
            if (size == 0) {
                _chunked = false;
            }
        }
    }

    // ------------------------
    void ESPWebDAV::sendContent_P(PGM_P content) {
        // ------------------------
        const char *footer = "\r\n";
        size_t size = strlen_P(content);

        if (_chunked) {
            char *chunkSize = (char *)malloc(11);
            if (chunkSize) {
                sprintf(chunkSize, "%x%s", size, footer);
                client.write(chunkSize, strlen(chunkSize));
                free(chunkSize);
            }
        }

        client.write_P(content, size);

        if (_chunked) {
            client.write(footer, 2);
            if (size == 0) {
                _chunked = false;
            }
        }
    }

    // ------------------------
    void ESPWebDAV::setContentLength(size_t len) {
        // ------------------------
        _contentLength = len;
    }

    // ------------------------
    size_t ESPWebDAV::readBytesWithTimeout(uint8_t *buf, size_t bufSize) {
        // ------------------------
        int timeout_ms = HTTP_MAX_POST_WAIT;
        size_t numAvailable = 0;
        int yieldCounter = 0;
        while (!(numAvailable = client.available()) && client.connected() && timeout_ms--) {
            if (++yieldCounter >= 100) {  // Yield every 100ms
                yield();
                yieldCounter = 0;
            }
            delay(1);
        }

        if (!numAvailable)
            return 0;

        size_t toRead = numAvailable < bufSize ? numAvailable : bufSize;
        int r = client.read(buf, toRead);
        return r > 0 ? (size_t)r : 0;
    }

    // ------------------------
    size_t ESPWebDAV::readBytesWithTimeout(uint8_t *buf, size_t bufSize, size_t numToRead) {
        // ------------------------
        int timeout_ms = HTTP_MAX_POST_WAIT;
        size_t numAvailable = 0;
        int yieldCounter = 0;

        while (((numAvailable = client.available()) < numToRead) && client.connected() && timeout_ms--) {
            if (++yieldCounter >= 100) {  // Yield every 100ms
                yield();
                yieldCounter = 0;
            }
            delay(1);
        }

        if (!numAvailable)
            return 0;

        size_t toRead = numToRead;
        if (toRead > bufSize)
            toRead = bufSize;
        if (toRead > numAvailable)
            toRead = numAvailable;
        int r = client.read(buf, toRead);
        return r > 0 ? (size_t)r : 0;
    }

    // ------------------------
    // Check if SD card is mounted
    // return -1 if can't get bus
    // 0 if card is not mounted
    // 1 if card is mounted
    int ESPWebDAV::cardMounted() {
        // ------------------------
        // Printer is holding the bus → we cannot use SD
        if (!sdcontrol.canWeTakeBus()) {
            return -1;
        }

        int mounted = 0;
        sdcontrol.takeBusControl();

        if (initSD(SD_CS, SD_SPI_SPEED)) {
            SdFile root;
            if (root.open("/", O_READ)) {
                mounted = 1; // root opened → card mounted
                root.close();
            } else {
                mounted = 0; // init worked, but root could not be opened
            }
        } else {
            mounted = 0; // init failed
        }

        sdcontrol.relinquishBusControl();
        return mounted;
    }
    // ------------------------
    // Try to add HTTP file list and upload/download
    // ------------------------
    void ESPWebDAV::handleHttp(THandlerFunction handler, String message) {
        DBG_PRINTLN("handleHttp");
        int m = cardMounted();
        switch (m) {
        case -1: {
            String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>SD Card Busy</title>";
            html += "<script>"
                    "const initialStatus=-1;"
                    "setInterval(function(){"
                    "fetch('/cardstatus',{cache:'no-store'})"
                    ".then(function(r){return r.text();})"
                    ".then(function(t){if(parseInt(t,10)!==initialStatus){location.reload();}})"
                    ".catch(function(){});"
                    "},3000);"
                    "</script>";
            html += "<style>:root{--bg:#f4f7f8;--panel:#ffffff;--ink:#24323a;--line:#d6e0e4;}body{margin:0;background:radial-gradient(circle at 15% 10%,#e9f8f9 0,#f4f7f8 45%,#edf4f6 100%);font-family:'Segoe UI',Tahoma,sans-serif;color:var(--ink);}main{max-width:1120px;margin:18px auto;padding:18px;background:var(--panel);border:1px solid var(--line);border-radius:14px;box-shadow:0 8px 24px rgba(19,37,48,0.08);}</style>";
            html += "</head><body><main>";
            html += "<h1>Files on SD Card</h1>";
            html += "<p style='color:red'>⚠ The printer is using SD card.</p>";
            html += "</main></body></html>";
            send("200", "text/html", html);
            return;
            break;
        }
        case 0: {
            String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>No SD</title>";
            html += "<script>"
                    "const initialStatus=0;"
                    "setInterval(function(){"
                    "fetch('/cardstatus',{cache:'no-store'})"
                    ".then(function(r){return r.text();})"
                    ".then(function(t){if(parseInt(t,10)!==initialStatus){location.reload();}})"
                    ".catch(function(){});"
                    "},3000);"
                    "</script>";
            html += "<style>:root{--bg:#f4f7f8;--panel:#ffffff;--ink:#24323a;--line:#d6e0e4;}body{margin:0;background:radial-gradient(circle at 15% 10%,#e9f8f9 0,#f4f7f8 45%,#edf4f6 100%);font-family:'Segoe UI',Tahoma,sans-serif;color:var(--ink);}main{max-width:1120px;margin:18px auto;padding:18px;background:var(--panel);border:1px solid var(--line);border-radius:14px;box-shadow:0 8px 24px rgba(19,37,48,0.08);}table{border-collapse:collapse;width:100%;}th,td{border:1px solid #ccc;padding:8px;text-align:left;}th{background:#eee;}</style>";
            html += "</head><body><main>";
            html += "<h1>Files on SD Card</h1>";
            html += "<p style='color:red'>SD card is not plugged in.</p>";
            html += "<table id='fileTable'><tr><th>Filename</th><th>Size (bytes)</th><th>Last Modified</th></tr></table>";
            html += "</main></body></html>";
            send("200", "text/html", html);
            return;
            break;
        }
        default: {
            (this->*handler)(message);
            break;
        }
        }
    }

    // ------------------------
    void ESPWebDAV::handleFileList(String message) {
        // ------------------------
        DBG_PRINTLN("handleFileList");
        String sortCol = "name";
        String sortOrder = "asc";
        String pageSizeParam = "50";
        String currentPath = "/";
        int page = 1;
        bool diagnostics = false;

        int qStart = uri.indexOf('?');
        if (qStart >= 0) {
            String query = uri.substring(qStart + 1);
            while (query.length() > 0) {
                int amp = query.indexOf('&');
                String pair = (amp >= 0) ? query.substring(0, amp) : query;
                query = (amp >= 0) ? query.substring(amp + 1) : "";

                int eq = pair.indexOf('=');
                if (eq < 0)
                    continue;

                String key = urlDecode(pair.substring(0, eq));
                String value = urlDecode(pair.substring(eq + 1));

                if (key == "sort") {
                    sortCol = value;
                } else if (key == "order") {
                    sortOrder = value;
                } else if (key == "page") {
                    page = value.toInt();
                } else if (key == "page_size") {
                    pageSizeParam = value;
                } else if (key == "path") {
                    currentPath = value;
                } else if (key == "diag") {
                    diagnostics = (value == "1" || value == "true" || value == "on");
                }
            }
        }

        if (!(sortCol == "name" || sortCol == "size" || sortCol == "date"))
            sortCol = "name";
        if (!(sortOrder == "asc" || sortOrder == "desc"))
            sortOrder = "asc";
        if (!(pageSizeParam == "50" || pageSizeParam == "100" || pageSizeParam == "all"))
            pageSizeParam = "50";
        if (page < 1)
            page = 1;
        currentPath = normalizeDirPath("/", currentPath);

        size_t selectedPageSize = (pageSizeParam == "100") ? 100 : 50;
        bool showAllEntries = (pageSizeParam == "all");

        if (!checkCardBusOrReject())
            return;

        sdcontrol.takeBusControl();
        SdFile root;
        if (!SdFileUtils::openDirectory(sd, root, currentPath)) {
            currentPath = "/";
            if (!SdFileUtils::openDirectory(sd, root, "/")) {
                send("500", "text/plain", "Cannot open root");
                sdcontrol.relinquishBusControl();
                return;
            }
        }
            size_t entryCapacity = 0;
            String* entryName = nullptr;
            uint32_t* entrySize = nullptr;
            uint32_t* entryDate = nullptr;
            bool* entryIsDir = nullptr;
            uint16_t* entryDirIndex = nullptr;

            auto ensureEntryCapacity = [&](size_t required) -> bool {
                if (required <= entryCapacity) {
                    return true;
                }

                size_t newCap = entryCapacity == 0 ? 64 : entryCapacity;
                while (newCap < required) {
                    newCap *= 2;
                }

                String* newName = new(std::nothrow) String[newCap];
                uint32_t* newSize = new(std::nothrow) uint32_t[newCap];
                uint32_t* newDate = new(std::nothrow) uint32_t[newCap];
                bool* newIsDir = new(std::nothrow) bool[newCap];
                uint16_t* newDirIndex = new(std::nothrow) uint16_t[newCap];
                if (!newName || !newSize || !newDate || !newIsDir || !newDirIndex) {
                    delete[] newName;
                    delete[] newSize;
                    delete[] newDate;
                    delete[] newIsDir;
                    delete[] newDirIndex;
                    return false;
                }

                for (size_t i = 0; i < entryCapacity; i++) {
                    newName[i] = entryName[i];
                    newSize[i] = entrySize[i];
                    newDate[i] = entryDate[i];
                    newIsDir[i] = entryIsDir[i];
                    newDirIndex[i] = entryDirIndex[i];
                }

                delete[] entryName;
                delete[] entrySize;
                delete[] entryDate;
                delete[] entryIsDir;
                delete[] entryDirIndex;

                entryName = newName;
                entrySize = newSize;
                entryDate = newDate;
                entryIsDir = newIsDir;
                entryDirIndex = newDirIndex;
                entryCapacity = newCap;
                return true;
            };

            auto compareAt = [&](size_t a, size_t b) -> int {
                auto compareNameInsensitive = [](const String &lhs, const String &rhs) -> int {
                    size_t al = lhs.length();
                    size_t bl = rhs.length();
                    size_t n = al < bl ? al : bl;
                    for (size_t i = 0; i < n; i++) {
                        char ca = (char)tolower((unsigned char)lhs[i]);
                        char cb = (char)tolower((unsigned char)rhs[i]);
                        if (ca < cb) return -1;
                        if (ca > cb) return 1;
                    }
                    if (al < bl) return -1;
                    if (al > bl) return 1;
                    return 0;
                };

                int cmp = 0;
                if (sortCol == "size") {
                    if (entrySize[a] < entrySize[b]) cmp = -1;
                    else if (entrySize[a] > entrySize[b]) cmp = 1;
                    else cmp = compareNameInsensitive(entryName[a], entryName[b]);
                } else if (sortCol == "date") {
                    if (entryDate[a] < entryDate[b]) cmp = -1;
                    else if (entryDate[a] > entryDate[b]) cmp = 1;
                    else cmp = compareNameInsensitive(entryName[a], entryName[b]);
                } else {
                    cmp = compareNameInsensitive(entryName[a], entryName[b]);
                }

                if (cmp == 0 && entryIsDir[a] != entryIsDir[b]) {
                    cmp = entryIsDir[a] ? -1 : 1;
                }

                if (sortOrder == "desc") cmp = -cmp;
                return cmp;
            };


            size_t totalEntries = 0;
            size_t nameFromRawLfn = 0;
            size_t lfnEntryCount = 0;
            size_t lfnResolvedViaGetName = 0;
            size_t sfnResolvedViaGetName = 0;
            size_t nameFromPrintName = 0;
            size_t nameFromSfnFallback = 0;
            size_t emptyNameCount = 0;
            String diagSamples = "";
            size_t diagSampleCount = 0;
            unsigned long loopYieldAt = millis();
            uint16_t* lfnMapDirIndex = nullptr;
            String* lfnMapName = nullptr;
            bool outOfMemory = false;

            SdFileUtils::scanDirectoryEntries(root, [&](const dir_t &dir, const String &resolvedName, uint16_t dirIndex) -> bool {
                if (!ensureEntryCapacity(totalEntries + 1)) {
                    outOfMemory = true;
                    return false;
                }

                entryDirIndex[totalEntries] = dirIndex;
                entryName[totalEntries] = resolvedName.length() > 0 ? resolvedName : String("<unnamed>");
                if (entryName[totalEntries] == "<unnamed>") {
                    emptyNameCount++;
                }

                bool isDir = DIR_IS_SUBDIR(&dir);
                entryIsDir[totalEntries] = isDir;
                entrySize[totalEntries] = isDir ? 0 : SdFileUtils::fileSizeFromDir(dir);

                char shortName[14];
                SdFileUtils::shortNameFromDir(dir, shortName, sizeof(shortName));
                const char *nameSource = (String(shortName) == resolvedName) ? "sfn" : "rawLFN";
                if (String(shortName) == resolvedName) {
                    sfnResolvedViaGetName++;
                } else {
                    nameFromRawLfn++;
                }

                if (diagnostics && diagSampleCount < 8) {
                    diagSamples += "<li>";
                    diagSamples += isDir ? "DIR" : "FILE";
                    diagSamples += " | source=";
                    diagSamples += nameSource;
                    diagSamples += " | name=";
                    diagSamples += entryName[totalEntries];
                    diagSamples += "</li>";
                    diagSampleCount++;
                }

                tm tmStr;
                tmStr.tm_hour = FAT_HOUR(dir.lastWriteTime);
                tmStr.tm_min = FAT_MINUTE(dir.lastWriteTime);
                tmStr.tm_sec = FAT_SECOND(dir.lastWriteTime);
                tmStr.tm_year = FAT_YEAR(dir.lastWriteDate) - 1900;
                tmStr.tm_mon = FAT_MONTH(dir.lastWriteDate) - 1;
                tmStr.tm_mday = FAT_DAY(dir.lastWriteDate);
                entryDate[totalEntries] = (uint32_t)mktime(&tmStr);
                totalEntries++;

                if (millis() - loopYieldAt > 15) {
                    yield();
                    loopYieldAt = millis();
                }
                return true;
            });

            if (outOfMemory) {
                send("500", "text/plain", "Out of memory while reading file list");
                root.close();
                delete[] entryName;
                delete[] entrySize;
                delete[] entryDate;
                delete[] entryIsDir;
                delete[] entryDirIndex;
                sdcontrol.relinquishBusControl();
                return;
            }

            auto swapAt = [&](size_t a, size_t b) {
                String tName = entryName[a];
                entryName[a] = entryName[b];
                entryName[b] = tName;

                uint32_t tSize = entrySize[a];
                entrySize[a] = entrySize[b];
                entrySize[b] = tSize;

                uint32_t tDate = entryDate[a];
                entryDate[a] = entryDate[b];
                entryDate[b] = tDate;

                bool tDir = entryIsDir[a];
                entryIsDir[a] = entryIsDir[b];
                entryIsDir[b] = tDir;

                uint16_t tDirIndex = entryDirIndex[a];
                entryDirIndex[a] = entryDirIndex[b];
                entryDirIndex[b] = tDirIndex;
            };

            // Global sort once with quicksort (O(n log n)) to avoid hangs on large directories.
            auto quickSort = [&](auto&& self, int left, int right) -> void {
                int i = left;
                int j = right;
                size_t pivot = (size_t)((left + right) / 2);

                while (i <= j) {
                    while (compareAt((size_t)i, pivot) < 0) {
                        i++;
                        if (millis() - loopYieldAt > 15) {
                            yield();
                            loopYieldAt = millis();
                        }
                    }
                    while (compareAt((size_t)j, pivot) > 0) {
                        j--;
                        if (millis() - loopYieldAt > 15) {
                            yield();
                            loopYieldAt = millis();
                        }
                    }

                    if (i <= j) {
                        if (i != j) {
                            swapAt((size_t)i, (size_t)j);
                            if ((size_t)i == pivot) pivot = (size_t)j;
                            else if ((size_t)j == pivot) pivot = (size_t)i;
                        }
                        i++;
                        j--;
                    }
                }

                if (left < j) self(self, left, j);
                if (i < right) self(self, i, right);
            };

            if (totalEntries > 1) {
                quickSort(quickSort, 0, (int)totalEntries - 1);
            }

            size_t pageSize = showAllEntries ? (totalEntries == 0 ? 1 : totalEntries) : selectedPageSize;
            size_t totalPages = (totalEntries == 0) ? 1 : ((totalEntries + pageSize - 1) / pageSize);
            if ((size_t)page > totalPages)
                page = totalPages;

            size_t startIndex = ((size_t)page - 1) * pageSize;
            size_t endIndex = startIndex + pageSize;
            if (endIndex > totalEntries)
                endIndex = totalEntries;

            auto urlEncodeComponentFwd = [&](const String &input) -> String {
                static const char hex[] = "0123456789ABCDEF";
                String out;
                out.reserve(input.length() * 3);
                for (size_t i = 0; i < input.length(); i++) {
                    uint8_t c = (uint8_t)input[i];
                    bool unreserved = (c >= 'A' && c <= 'Z') ||
                                      (c >= 'a' && c <= 'z') ||
                                      (c >= '0' && c <= '9') ||
                                      c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
                    if (unreserved) {
                        out += (char)c;
                    } else {
                        out += '%';
                        out += hex[(c >> 4) & 0x0F];
                        out += hex[c & 0x0F];
                    }
                }
                return out;
            };

            String encodedPath = urlEncodeComponentFwd(currentPath);
            String baseQuery = "path=" + encodedPath + "&sort=" + sortCol + "&order=" + sortOrder + "&page_size=" + pageSizeParam;
            if (diagnostics)
                baseQuery += "&diag=1";
            String pagePrefix = "/?" + baseQuery + "&page=";

            String nextNameOrder = (sortCol == "name" && sortOrder == "asc") ? "desc" : "asc";
            String nextSizeOrder = (sortCol == "size" && sortOrder == "asc") ? "desc" : "asc";
            String nextDateOrder = (sortCol == "date" && sortOrder == "asc") ? "desc" : "asc";
            String nameArrow = (sortCol == "name") ? (sortOrder == "asc" ? " ^" : " v") : "";
            String sizeArrow = (sortCol == "size") ? (sortOrder == "asc" ? " ^" : " v") : "";
            String dateArrow = (sortCol == "date") ? (sortOrder == "asc" ? " ^" : " v") : "";

            auto sendPager = [&](bool withJump) {
                if (totalPages <= 1) {
                    return;
                }

                sendContent("<div class='pager'>");
                if (page > 1) {
                    sendContent("<a class='btn' href='" + pagePrefix + String(page - 1) + "'>&lt; Prev</a>");
                } else {
                    sendContent("<span class='btn disabled'>&lt; Prev</span>");
                }

                size_t maxPageLinks = 9;
                size_t startPage = 1;
                if (totalPages > maxPageLinks && (size_t)page > maxPageLinks / 2) {
                    startPage = (size_t)page - maxPageLinks / 2;
                }
                if (startPage + maxPageLinks - 1 > totalPages) {
                    startPage = totalPages > maxPageLinks ? totalPages - maxPageLinks + 1 : 1;
                }
                size_t endPage = startPage + maxPageLinks - 1;
                if (endPage > totalPages) endPage = totalPages;

                for (size_t p = startPage; p <= endPage; p++) {
                    if ((int)p == page) {
                        sendContent("<span class='btn active'>" + String((unsigned long)p) + "</span>");
                    } else {
                        sendContent("<a class='btn' href='" + pagePrefix + String((unsigned long)p) + "'>" + String((unsigned long)p) + "</a>");
                    }
                    if (millis() - loopYieldAt > 15) {
                        yield();
                        loopYieldAt = millis();
                    }
                }

                if ((size_t)page < totalPages) {
                    sendContent("<a class='btn' href='" + pagePrefix + String(page + 1) + "'>Next &gt;</a>");
                } else {
                    sendContent("<span class='btn disabled'>Next &gt;</span>");
                }

                if (withJump) {
                    sendContent("<form class='jump' method='get' action='/'><input type='hidden' name='path' value='" + currentPath + "'><input type='hidden' name='sort' value='" + sortCol + "'><input type='hidden' name='order' value='" + sortOrder + "'><input type='hidden' name='page_size' value='" + pageSizeParam + "'>");
                    if (diagnostics)
                        sendContent("<input type='hidden' name='diag' value='1'>");
                    sendContent("<label for='pageJump'>Page</label><input id='pageJump' type='number' name='page' min='1' max='" + String((unsigned long)totalPages) + "' value='" + String(page) + "'><button class='btn' type='submit'>Go</button></form>");
                }

                sendContent("</div>");
            };

            auto urlEncodeComponent = [&](const String &input) -> String {
                static const char hex[] = "0123456789ABCDEF";
                String out;
                out.reserve(input.length() * 3);
                for (size_t i = 0; i < input.length(); i++) {
                    uint8_t c = (uint8_t)input[i];
                    bool unreserved = (c >= 'A' && c <= 'Z') ||
                                      (c >= 'a' && c <= 'z') ||
                                      (c >= '0' && c <= '9') ||
                                      c == '-' || c == '_' || c == '.' || c == '~';
                    if (unreserved) {
                        out += (char)c;
                    } else {
                        out += '%';
                        out += hex[(c >> 4) & 0x0F];
                        out += hex[c & 0x0F];
                    }
                }
                return out;
            };

            setContentLength(CONTENT_LENGTH_UNKNOWN);
            send("200", "text/html", "");

            sendContent("<!DOCTYPE html><html><head><meta charset='utf-8'><title>SD Card Files</title>");
            sendContent("<style>:root{--bg:#f4f7f8;--panel:#ffffff;--ink:#24323a;--muted:#57707b;--line:#d6e0e4;--accent:#0f8b8d;--accent2:#ffb703;--row:#f9fcfd;--row2:#eef6f8;}*{box-sizing:border-box;}body{margin:0;background:radial-gradient(circle at 15% 10%,#e9f8f9 0,#f4f7f8 45%,#edf4f6 100%);font-family:'Segoe UI',Tahoma,sans-serif;color:var(--ink);}main{max-width:1120px;margin:18px auto;padding:18px;background:var(--panel);border:1px solid var(--line);border-radius:14px;box-shadow:0 8px 24px rgba(19,37,48,0.08);}h1{margin:0 0 14px 0;font-size:28px;letter-spacing:.2px;}a{color:#0d6e70;}table{border-collapse:collapse;width:100%;margin-top:8px;}th,td{border:1px solid var(--line);padding:10px;text-align:left;}th{background:linear-gradient(90deg,#e8f4f5,#e1f0f2);}th a{color:inherit;text-decoration:none;display:block;}tbody tr:nth-child(odd){background:var(--row);}tbody tr:nth-child(even){background:var(--row2);} .toolbar{margin:10px 0;color:var(--muted);display:flex;flex-wrap:wrap;align-items:center;gap:10px;} .toolbar a{margin-right:8px;} .btn{display:inline-block;padding:6px 11px;border:1px solid #9cb9c2;border-radius:8px;background:#fff;color:#1b3a45;text-decoration:none;margin:2px;} .btn:hover{background:#f1f8fb;} .btn.active{background:linear-gradient(90deg,var(--accent),#0a9396);color:#fff;border-color:#0a7f82;} .btn.disabled{opacity:.45;pointer-events:none;} .pager{margin:12px 0;display:flex;flex-wrap:wrap;align-items:center;gap:4px;} .jump{display:inline-flex;align-items:center;gap:6px;margin-left:10px;} .jump input[type=number]{width:84px;padding:6px;border:1px solid #a7c1c9;border-radius:8px;} .topbar{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;gap:10px;margin-bottom:10px;} .badge{display:inline-block;padding:5px 10px;border-radius:999px;background:#e9f7f7;color:#0a6d6f;border:1px solid #b9dede;} button{padding:7px 12px;border-radius:8px;border:1px solid #8fb1bd;background:linear-gradient(90deg,#0f8b8d,#0a9396);color:#fff;cursor:pointer;} input[type=file]{max-width:260px;} @media (max-width:700px){main{margin:0;border-radius:0;border:none;}h1{font-size:22px;}th,td{padding:8px;font-size:13px;}}</style>");
            String uploadPathJs = currentPath;
            uploadPathJs.replace("'", "");
            sendContent("<script>var uploadInProgress=false;var currentDirPath='" + uploadPathJs + "';function clearUploadInput(){var f=document.getElementById('uploadFile');if(f){f.value='';}}window.addEventListener('pageshow',function(){clearUploadInput();});function uploadFile(){var f=document.getElementById('uploadFile');if(!f||f.files.length==0)return;uploadInProgress=true;var file=f.files[0];document.getElementById('uploadProgress').style.display='block';var bar=document.getElementById('progressBar');bar.style.width='0%';bar.innerHTML='0%';var xhr=new XMLHttpRequest();xhr.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round((e.loaded/e.total)*100);bar.style.width=p+'%';bar.innerHTML=p+'% ('+Math.round(e.loaded/1024)+' KB / '+Math.round(e.total/1024)+' KB)';}};xhr.onload=function(){uploadInProgress=false;clearUploadInput();document.getElementById('uploadProgress').style.display='none';if(xhr.status==200){alert('Upload finished');location.reload();}else{alert('Upload error: '+xhr.responseText);}};xhr.onerror=function(){uploadInProgress=false;clearUploadInput();document.getElementById('uploadProgress').style.display='none';alert('Upload error: Network failure or server unreachable');};xhr.open('POST','/upload?path='+encodeURIComponent(currentDirPath)+'&name='+encodeURIComponent(file.name),true);xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.send(file);}setInterval(function(){if(uploadInProgress)return;fetch('/cardstatus',{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){if(parseInt(t,10)!==1){location.reload();}}).catch(function(){});},3000);</script>");
            sendContent("</head><body><main><div class='topbar'><h1>Files on SD Card</h1><div><span class='badge'>Live card monitoring</span> <a class='btn' href='/settings'>Settings</a> <a class='btn' href='/status'>Status</a></div></div>");
            sendContent("<div class='toolbar'>Path: <strong>" + currentPath + "</strong></div>");
            sendContent("<div class='toolbar'><input type='file' id='uploadFile'><button onclick='uploadFile()'>Upload</button></div>");
            sendContent("<div class='toolbar' id='uploadProgress' style='display:none;'><div style='background:#e0e0e0;border-radius:8px;overflow:hidden;height:24px;width:100%;max-width:500px;'><div id='progressBar' style='background:linear-gradient(90deg,#0f8b8d,#0a9396);height:100%;width:0%;color:#fff;text-align:center;line-height:24px;transition:width 0.3s;'>0%</div></div></div>");
            if (diagnostics) {
                String diagHtml = "<div class='toolbar' style='border:1px solid #999;background:#f8f8f8;padding:8px'>";
                diagHtml += "<strong>Diagnostic:</strong> ";
                diagHtml += "lfnEntries=" + String((unsigned long)lfnEntryCount);
                diagHtml += " | rawLFNMap=" + String((unsigned long)nameFromRawLfn);
                diagHtml += " | getNameLFN=" + String((unsigned long)lfnResolvedViaGetName);
                diagHtml += " | getNameSFN=" + String((unsigned long)sfnResolvedViaGetName);
                diagHtml += " | printName=" + String((unsigned long)nameFromPrintName);
                diagHtml += " | sfnFallback=" + String((unsigned long)nameFromSfnFallback);
                diagHtml += " | unnamed=" + String((unsigned long)emptyNameCount);
                if (diagSamples.length() > 0) {
                    diagHtml += "<br>Samples:<ul style='margin:6px 0 0 20px'>" + diagSamples + "</ul>";
                }
                diagHtml += "</div>";
                sendContent(diagHtml);
            }

            String sizeControls = "<div class='toolbar'>Page size: ";
            if (pageSizeParam == "50")
                sizeControls += "<strong>50</strong> ";
            else
                sizeControls += "<a href='/?path=" + encodedPath + "&sort=" + sortCol + "&order=" + sortOrder + "&page=1&page_size=50" + (diagnostics ? "&diag=1" : "") + "'>50</a> ";

            if (pageSizeParam == "100")
                sizeControls += "<strong>100</strong> ";
            else
                sizeControls += "<a href='/?path=" + encodedPath + "&sort=" + sortCol + "&order=" + sortOrder + "&page=1&page_size=100" + (diagnostics ? "&diag=1" : "") + "'>100</a> ";

            if (pageSizeParam == "all")
                sizeControls += "<strong>All</strong> ";
            else
                sizeControls += "<a href='/?path=" + encodedPath + "&sort=" + sortCol + "&order=" + sortOrder + "&page=1&page_size=all" + (diagnostics ? "&diag=1" : "") + "'>All</a> ";
            sizeControls += "</div>";
            sendContent(sizeControls);

            String pageInfo = "<div class='toolbar'>Entries: " + String((unsigned long)totalEntries) + " | Page " + String(page) + " of " + String((unsigned long)totalPages) + "</div>";
            sendContent(pageInfo);

            sendPager(false);

            sendContent("<table id='fileTable'><thead><tr>");
            sendContent("<th><a href='/?path=" + encodedPath + "&sort=name&order=" + nextNameOrder + "&page=1&page_size=" + pageSizeParam + (diagnostics ? "&diag=1" : "") + "'>Filename" + nameArrow + "</a></th>");
            sendContent("<th><a href='/?path=" + encodedPath + "&sort=size&order=" + nextSizeOrder + "&page=1&page_size=" + pageSizeParam + (diagnostics ? "&diag=1" : "") + "'>Size (bytes)" + sizeArrow + "</a></th>");
            sendContent("<th><a href='/?path=" + encodedPath + "&sort=date&order=" + nextDateOrder + "&page=1&page_size=" + pageSizeParam + (diagnostics ? "&diag=1" : "") + "'>Last Modified" + dateArrow + "</a></th>");
            sendContent("<th>Actions</th>");
            sendContent("</tr></thead><tbody>");

            if (currentPath != "/") {
                String parentPath = normalizeDirPath(currentPath, "..");
                String parentLink = "/?path=" + urlEncodeComponentFwd(parentPath) + "&sort=" + sortCol + "&order=" + sortOrder + "&page=1&page_size=" + pageSizeParam + (diagnostics ? "&diag=1" : "");
                sendContent("<tr><td><a href='" + parentLink + "'>[DIR] ..</a></td><td>-</td><td>-</td><td></td></tr>");
            }

            if (totalEntries == 0) {
                sendContent("<tr><td colspan='4'>No files found on SD card.</td></tr>");
            } else {
                for (size_t i = startIndex; i < endIndex; i++) {
                    time_t t2t = (time_t)entryDate[i];
                    t2t += (time_t)network.timezoneMinutes() * 60;
                    tm *gTm = gmtime(&t2t);
                    char buf[20];
                    if (gTm)
                        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gTm);
                    else {
                        strncpy(buf, "1970-01-01 00:00:00", sizeof(buf));
                        buf[sizeof(buf) - 1] = '\0';
                    }

                    if (entryIsDir[i]) {
                        String childPath = (currentPath == "/" ? "/" : currentPath + "/") + entryName[i];
                        String dirLink = "/?path=" + urlEncodeComponentFwd(childPath) + "&sort=" + sortCol + "&order=" + sortOrder + "&page=1&page_size=" + pageSizeParam + (diagnostics ? "&diag=1" : "");
                        sendContent("<tr><td><a href='" + dirLink + "'>[DIR] " + entryName[i] + "</a></td><td>-</td><td>" + String(buf) + "</td><td></td></tr>");
                    } else {
                        String encodedName = urlEncodeComponent(entryName[i]);
                        String deleteAction = "/delete?path=" + encodedPath + "&idx=" + String((unsigned long)entryDirIndex[i]) + "&name=" + encodedName;
                        sendContent("<tr><td><a href='/download?path=" + encodedPath + "&idx=" + String((unsigned long)entryDirIndex[i]) + "&name=" + encodedName + "'>" + entryName[i] + "</a></td><td>" + String(entrySize[i]) + "</td><td>" + String(buf) + "</td><td><form method='post' action='" + deleteAction + "' onsubmit=\"return confirm('Delete this file?');\" style='margin:0'><button type='submit'>Delete</button></form></td></tr>");
                    }

                    if (millis() - loopYieldAt > 15) {
                        yield();
                        loopYieldAt = millis();
                    }
                }
            }

            sendContent("</tbody></table>");

            sendPager(true);

            sendContent("</main></body></html>");

            root.close();
            delete[] entryName;
            delete[] entrySize;
            delete[] entryDate;
            delete[] entryIsDir;
            delete[] entryDirIndex;
            delete[] lfnMapDirIndex;
            delete[] lfnMapName;
            sdcontrol.relinquishBusControl();
    }

    void ESPWebDAV::handleFileDownload(String message) {
        if (method != "GET") {
            send("405", "text/plain", "Method Not Allowed");
            return;
        }

        int idxPos = uri.indexOf("?idx=");
        if (idxPos < 0)
            idxPos = uri.indexOf("&idx=");
        if (idxPos >= 0) {
            String idxStr = urlDecode(uri.substring(idxPos + 5));
            int amp = idxStr.indexOf('&');
            if (amp >= 0)
                idxStr = idxStr.substring(0, amp);

            String queryName = "";
            int namePos = uri.indexOf("&name=");
            if (namePos >= 0) {
                queryName = urlDecode(uri.substring(namePos + 6));
                int ampName = queryName.indexOf('&');
                if (ampName >= 0) {
                    queryName = queryName.substring(0, ampName);
                }
            }

            String dirPath = "/";
            int pathPos = uri.indexOf("?path=");
            if (pathPos < 0)
                pathPos = uri.indexOf("&path=");
            if (pathPos >= 0) {
                dirPath = urlDecode(uri.substring(pathPos + 6));
                int ampPath = dirPath.indexOf('&');
                if (ampPath >= 0)
                    dirPath = dirPath.substring(0, ampPath);
            }
            dirPath = normalizeDirPath("/", dirPath);

            uint16_t dirIndex = (uint16_t)idxStr.toInt();
            if (!checkCardBusOrReject())
                return;
            sdcontrol.takeBusControl();

            SdFile root;
            if (!SdFileUtils::openDirectory(sd, root, dirPath)) {
                if (!SdFileUtils::openDirectory(sd, root, "/")) {
                    send("500", "text/plain", "Cannot open root");
                    sdcontrol.relinquishBusControl();
                    return;
                }
            }

            SdFile file;
            if (!file.open(&root, dirIndex, O_READ) || file.isDir()) {
                root.close();
                send("404", "text/plain", "File not found");
                sdcontrol.relinquishBusControl();
                return;
            }

            char dlNameBuf[256];
            String downloadName = queryName;
            if (downloadName.length() == 0 && file.getName(dlNameBuf, sizeof(dlNameBuf)) && dlNameBuf[0] != '\0') {
                downloadName = String(dlNameBuf);
            }
            if (downloadName.length() == 0) {
                downloadName = "download.bin";
            }
            downloadName.replace("\r", "");
            downloadName.replace("\n", "");
            downloadName.replace("\"", "_");

            String header = "HTTP/1.1 200 OK\r\n";
            header += "Content-Type: application/octet-stream\r\n";
            header += "Content-Length: " + String(file.fileSize()) + "\r\n";
            header += "Content-Disposition: attachment; filename=\"" + downloadName + "\"\r\n";
            header += "Connection: close\r\n\r\n";
            client.write(header.c_str(), header.length());

            uint8_t buf[256];
            int n;
            while ((n = file.read(buf, sizeof(buf))) > 0) {
                client.write(buf, n);
            }

            file.close();
            root.close();
            sdcontrol.relinquishBusControl();
            return;
        }

        // Extract filename from URL: /download?name=filename.txt (optionally with &path=dir)
        int idx = uri.indexOf("?name=");
        if (idx < 0)
            idx = uri.indexOf("&name=");
        if (idx == -1) {
            send("400", "text/plain", "No filename specified");
            return;
        }

        String filename = urlDecode(uri.substring(idx + 6));
        int amp = filename.indexOf('&');
        if (amp >= 0)
            filename = filename.substring(0, amp);
        if (!SdFileUtils::isSafeFileName(filename)) {
            send("400", "text/plain", "Invalid filename");
            return;
        }

        String dirPath = "/";
        int pathPos = uri.indexOf("?path=");
        if (pathPos < 0)
            pathPos = uri.indexOf("&path=");
        if (pathPos >= 0) {
            dirPath = urlDecode(uri.substring(pathPos + 6));
            int ampPath = dirPath.indexOf('&');
            if (ampPath >= 0)
                dirPath = dirPath.substring(0, ampPath);
        }
        dirPath = normalizeDirPath("/", dirPath);
        String fullPath = SdFileUtils::joinPath(dirPath, filename);

        if (!checkCardBusOrReject())
            return;
        sdcontrol.takeBusControl();

        SdFile file;
        if (!SdFileUtils::openFileRead(sd, file, fullPath)) {
            send("404", "text/plain", "File not found");
            sdcontrol.relinquishBusControl();
            return;
        }

        // Send HTTP headers for download
        String header = "HTTP/1.1 200 OK\r\n";
        header += "Content-Type: application/octet-stream\r\n";
        header += "Content-Length: " + String(file.fileSize()) + "\r\n";
        header += "Content-Disposition: attachment; filename=\"" + filename + "\"\r\n";
        header += "Connection: close\r\n\r\n";
        client.write(header.c_str(), header.length());

        // Stream file in 256-byte chunks
        uint8_t buf[256];
        int n;
        while ((n = file.read(buf, sizeof(buf))) > 0) {
            client.write(buf, n);
        }

        file.close();
        sdcontrol.relinquishBusControl();
    }

    void ESPWebDAV::handleStatusPage(String message) {
        int initialStatus = cardMounted();
        String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
        html += "<title>ESP8266 Status</title>";
        html += "<style>:root{--bg:#f4f7f8;--panel:#ffffff;--ink:#24323a;--muted:#57707b;--line:#d6e0e4;--accent:#0f8b8d;--row:#f9fcfd;--row2:#eef6f8;}*{box-sizing:border-box;}body{margin:0;background:radial-gradient(circle at 15% 10%,#e9f8f9 0,#f4f7f8 45%,#edf4f6 100%);font-family:'Segoe UI',Tahoma,sans-serif;color:var(--ink);}main{max-width:1120px;margin:18px auto;padding:18px;background:var(--panel);border:1px solid var(--line);border-radius:14px;box-shadow:0 8px 24px rgba(19,37,48,0.08);}h1{margin:0 0 14px 0;font-size:28px;}table{border-collapse:collapse;width:100%;}td,th{border:1px solid var(--line);padding:10px;}th{background:linear-gradient(90deg,#e8f4f5,#e1f0f2);text-align:left;}tr:nth-child(odd){background:var(--row);}tr:nth-child(even){background:var(--row2);} .btn{display:inline-block;padding:6px 11px;border:1px solid #9cb9c2;border-radius:8px;background:#fff;color:#1b3a45;text-decoration:none;margin-right:8px;margin-top:10px;}</style>";
        html += "<script>const initialStatus=" + String(initialStatus) + ";setInterval(function(){fetch('/cardstatus',{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){if(parseInt(t,10)!==initialStatus){location.reload();}}).catch(function(){});},3000);</script>";
        html += "</head><body><main>";
        html += "<h1>ESP8266 Status</h1>";
        html += "<table>";

        html += "<tr><th>WiFi SSID</th><td>" + WiFi.SSID() + "</td></tr>";
        html += "<tr><th>IP Address</th><td>" + WiFi.localIP().toString() + "</td></tr>";
        html += "<tr><td>MAC Address</td><td>" + WiFi.macAddress() + "</td></tr>";
        html += "<tr><th>Signal Strength (RSSI)</th><td>" + String(WiFi.RSSI()) + " dBm</td></tr>";
        int ch = WiFi.channel();
        int freq = 2407 + ch * 5;
        html += "<tr><td>WiFi Channel</td><td>" + String(ch) + "</td></tr>";
        html += "<tr><td>WiFi Frequency</td><td>" + String(freq) + " MHz</td></tr>";
        html += "<tr><th>Free Heap</th><td>" + String(ESP.getFreeHeap()) + " bytes</td></tr>";

        // uptime in seconds
        unsigned long uptime = millis() / 1000;
        html += "<tr><th>Uptime</th><td>" + String(uptime) + " sec</td></tr>";
        html += "<tr><th>NTP Synced</th><td>" + String(network.isNtpSynced() ? "yes" : "no") + "</td></tr>";
        html += "<tr><th>Timezone Offset</th><td>" + String(((float)network.timezoneMinutes()) / 60.0f, 2) + " hours</td></tr>";
        html += "<tr><th>Current Epoch</th><td>" + String(network.currentEpoch()) + "</td></tr>";
        int m = initialStatus;
        String sdStatus;
        if (m == -1) {
            sdStatus = "<p style='color:red'>⚠ The printer is using SD card.</p>";
        } else if (m == 0) {
            sdStatus = "<p style='color:red'>⚠ No SD card detected.</p>";
        } else {
            sdStatus = "<p style='color:green'>SD card mounted and ready.</p>";
        }
        html += "<tr><th>SD Card</th><td>" + sdStatus + "</td></tr>";

        html += "</table>";
        html += "<a class='btn' href='/'>Browse Files</a>";
        html += "<a class='btn' href='/settings'>Time Settings</a>";
        html += "</main></body></html>";

        send("200", "text/html", html);
    }

    void ESPWebDAV::handleSettingsPage(String message) {
        String actionMessage = "";
        bool saveRequested = false;
        bool clearManual = false;
        bool hasManualEpoch = false;
        uint32_t manualEpoch = 0;
        int tzMinutes = network.timezoneMinutes();
        int initialStatus = cardMounted();

        int qStart = uri.indexOf('?');
        if (qStart >= 0) {
            String query = uri.substring(qStart + 1);
            while (query.length() > 0) {
                int amp = query.indexOf('&');
                String pair = (amp >= 0) ? query.substring(0, amp) : query;
                query = (amp >= 0) ? query.substring(amp + 1) : "";

                int eq = pair.indexOf('=');
                if (eq < 0)
                    continue;

                String key = urlDecode(pair.substring(0, eq));
                String value = urlDecode(pair.substring(eq + 1));

                if (key == "save") {
                    saveRequested = (value == "1" || value == "true" || value == "on");
                } else if (key == "tz_hours") {
                    tzMinutes = (int)(value.toFloat() * 60.0f);
                } else if (key == "manual_epoch") {
                    manualEpoch = (uint32_t)strtoul(value.c_str(), nullptr, 10);
                    hasManualEpoch = manualEpoch > 0;
                } else if (key == "clear_manual") {
                    clearManual = (value == "1" || value == "true" || value == "on");
                }
            }
        }

        if (saveRequested) {
            network.setTimezoneMinutes((int16_t)tzMinutes);
            if (clearManual) {
                network.clearManualEpoch();
                actionMessage = "Manual fallback time cleared.";
            } else if (hasManualEpoch) {
                if (network.applyManualEpoch(manualEpoch)) {
                    actionMessage = "Manual fallback time updated.";
                } else {
                    actionMessage = "Invalid manual time. Use a date after year 2000.";
                }
            } else {
                actionMessage = "Timezone updated.";
            }
            config.save();
        }

        uint32_t currentEpoch = network.currentEpoch();
        uint32_t savedManualEpoch = network.manualEpoch();

        String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Time Settings</title>";
        html += "<style>:root{--bg:#f4f7f8;--panel:#ffffff;--ink:#24323a;--muted:#57707b;--line:#d6e0e4;--accent:#0f8b8d;--row:#f9fcfd;--row2:#eef6f8;}*{box-sizing:border-box;}body{margin:0;background:radial-gradient(circle at 15% 10%,#e9f8f9 0,#f4f7f8 45%,#edf4f6 100%);font-family:'Segoe UI',Tahoma,sans-serif;color:var(--ink);}main{max-width:1120px;margin:18px auto;padding:18px;background:var(--panel);border:1px solid var(--line);border-radius:14px;box-shadow:0 8px 24px rgba(19,37,48,0.08);}h1{margin:0 0 14px 0;font-size:28px;}label{font-weight:600;display:block;margin-top:12px;}input[type=number],input[type=datetime-local]{width:100%;padding:10px;border:1px solid #a9c0c8;border-radius:8px;}button,a.btn{display:inline-block;margin-top:12px;padding:8px 14px;border-radius:9px;border:1px solid #7da8b4;background:linear-gradient(90deg,var(--accent),#0a9396);color:#fff;text-decoration:none;cursor:pointer;}a.btn.secondary{background:#fff;color:#1b3a45;} .note{color:#55707a;font-size:14px;} .ok{padding:10px;border-radius:9px;background:#e8f7ea;border:1px solid #b6dfbc;margin:10px 0;} .warn{padding:10px;border-radius:9px;background:#fff4df;border:1px solid #f3d39a;margin:10px 0;} .row{display:flex;gap:12px;flex-wrap:wrap;} .row > div{flex:1;min-width:220px;}</style>";
        html += "<script>const initialStatus=" + String(initialStatus) + ";setInterval(function(){fetch('/cardstatus',{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){if(parseInt(t,10)!==initialStatus){location.reload();}}).catch(function(){});},3000);function beforeSubmit(){var dt=document.getElementById('manualDateTime').value;var hidden=document.getElementById('manualEpoch');if(dt){hidden.value=Math.floor(new Date(dt).getTime()/1000);}else{hidden.value='';}return true;}</script>";
        html += "</head><body><main><h1>Time Settings</h1>";

        if (actionMessage.length() > 0) {
            html += "<div class='ok'>" + actionMessage + "</div>";
        }
        if (!network.isNtpSynced()) {
            html += "<div class='warn'>NTP is not currently synced. Manual fallback time will be used for timestamps if provided.</div>";
        }

        html += "<p class='note'>Current epoch: " + String(currentEpoch) + " | Saved manual epoch: " + String(savedManualEpoch) + "</p>";
        html += "<form method='get' action='/settings' onsubmit='return beforeSubmit()'>";
        html += "<input type='hidden' name='save' value='1'>";
        html += "<input type='hidden' id='manualEpoch' name='manual_epoch' value=''>";
        html += "<div class='row'><div><label for='tz'>Timezone offset (hours)</label><input id='tz' type='number' step='0.5' name='tz_hours' min='-12' max='14' value='" + String(((float)network.timezoneMinutes()) / 60.0f, 2) + "'></div>";
        html += "<div><label for='manualDateTime'>Manual date/time (local)</label><input id='manualDateTime' type='datetime-local' step='1'></div></div>";
        html += "<label><input type='checkbox' name='clear_manual' value='1'> Clear saved manual fallback time</label>";
        html += "<p class='note'>Tip: if NTP is unavailable, set manual date/time once so new files keep correct timestamps.</p>";
        html += "<button type='submit'>Save Settings</button> <a class='btn secondary' href='/'>Back to Files</a>";
        html += "</form></main></body></html>";

        send("200", "text/html", html);
    }

    void ESPWebDAV::handleCardStatus(String message) {
        int m = cardMounted();
        send("200", "text/plain", String(m));
    }

    void ESPWebDAV::handleFileUpload(String message) {
        DBG_PRINTLN("[DAV][UPLOAD] request received");
        if (method != "POST") {
            send("405", "text/plain", "Method Not Allowed");
            return;
        }

        int idx = uri.indexOf("?name=");
        if (idx < 0)
            idx = uri.indexOf("&name=");
        if (idx == -1) {
            send("400", "text/plain", "No filename specified");
            return;
        }

        String filename = urlDecode(uri.substring(idx + 6));
        int amp = filename.indexOf('&');
        if (amp >= 0)
            filename = filename.substring(0, amp);
        if (!SdFileUtils::isSafeFileName(filename)) {
            send("400", "text/plain", "Invalid filename");
            return;
        }

        String dirPath = "/";
        int pathPos = uri.indexOf("?path=");
        if (pathPos < 0)
            pathPos = uri.indexOf("&path=");
        if (pathPos >= 0) {
            dirPath = urlDecode(uri.substring(pathPos + 6));
            int ampPath = dirPath.indexOf('&');
            if (ampPath >= 0)
                dirPath = dirPath.substring(0, ampPath);
        }
        dirPath = normalizeDirPath("/", dirPath);
        String fullPath = SdFileUtils::joinPath(dirPath, filename);

        size_t totalLength = contentLengthHeader.toInt();
        if (totalLength == 0) {
            send("400", "text/plain", "Empty upload or missing Content-Length");
            return;
        }

        if (!checkCardBusOrReject()) {
            return;
        }
        sdcontrol.takeBusControl();
        DBG_PRINT("[DAV][UPLOAD] filename: ");
        DBG_PRINTLN(fullPath);

        SdFile file;
        DBG_PRINT("[DAV][UPLOAD] Attempting to open: ");
        DBG_PRINTLN(fullPath);
        DBG_PRINT("[DAV][UPLOAD] SD card mounted: ");
        DBG_PRINTLN(sd.card() ? "yes" : "no");
        
        // Remove leading slash if present - SdFat doesn't need it with vwd()
        String pathForOpen = SdFileUtils::toRelativePath(fullPath);
        DBG_PRINT("[DAV][UPLOAD] Path for open (without leading /): ");
        DBG_PRINTLN(pathForOpen);
        
        // Use shared upload function (same as WebDAV PUT)
        if (!uploadFileData(pathForOpen.c_str(), totalLength)) {
            send("500", "text/plain", "Upload failed");
            sdcontrol.relinquishBusControl();
            return;
        }
        
        send("200", "text/plain", "Upload complete: " + filename);
        sdcontrol.relinquishBusControl();
    }

    String ESPWebDAV::readLine() {
        String line = "";
        char c;
        while (client.available()) {
            c = client.read();
            if (c == '\r')
                continue;
            if (c == '\n')
                break;
            line += c;
        }
        return line;
    }

    // ------------------------
    void ESPWebDAV::handleFileDelete(String blank) {
        // ------------------------
        DBG_PRINTLN("[DAV][DELETE] request received");

        String pathParam = "/";
        String nameParam = "";
        String idxParam = "";

        int qStart = uri.indexOf('?');
        if (qStart >= 0) {
            String query = uri.substring(qStart + 1);
            while (query.length() > 0) {
                int amp = query.indexOf('&');
                String pair = (amp >= 0) ? query.substring(0, amp) : query;
                query = (amp >= 0) ? query.substring(amp + 1) : "";
                
                int eq = pair.indexOf('=');
                if (eq < 0)
                    continue;
                
                String key = urlDecode(pair.substring(0, eq));
                String value = urlDecode(pair.substring(eq + 1));
                
                if (key == "path") {
                    pathParam = value;
                } else if (key == "name") {
                    nameParam = value;
                } else if (key == "idx") {
                    idxParam = value;
                }
            }
        }

        if (!SdFileUtils::isSafeFileName(nameParam)) {
            send("400", "text/plain", "Invalid file name");
            return;
        }

        pathParam = normalizeDirPath("/", pathParam);
        String fullPath = SdFileUtils::joinPath(pathParam, nameParam);

        auto redirectToList = [&]() {
            static const char hex[] = "0123456789ABCDEF";
            String encodedPath;
            encodedPath.reserve(pathParam.length() * 3);
            for (size_t i = 0; i < pathParam.length(); i++) {
                uint8_t c = (uint8_t)pathParam[i];
                bool unreserved = (c >= 'A' && c <= 'Z') ||
                                  (c >= 'a' && c <= 'z') ||
                                  (c >= '0' && c <= '9') ||
                                  c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
                if (unreserved) {
                    encodedPath += (char)c;
                } else {
                    encodedPath += '%';
                    encodedPath += hex[(c >> 4) & 0x0F];
                    encodedPath += hex[c & 0x0F];
                }
            }
            String response = "HTTP/1.1 303 See Other\r\n";
            response += "Location: /?path=" + encodedPath + "\r\n";
            response += "Connection: close\r\n\r\n";
            client.write(response.c_str(), response.length());
        };

        DBG_PRINT("[DAV][DELETE] filename: ");
        DBG_PRINTLN(fullPath);

        if (!checkCardBusOrReject())
            return;
        sdcontrol.takeBusControl();

        if (idxParam.length() > 0) {
            uint16_t dirIndex = (uint16_t)idxParam.toInt();
            SdFile root;
            if (!SdFileUtils::openDirectory(sd, root, pathParam)) {
                root.close();
                sdcontrol.relinquishBusControl();
                redirectToList();
                return;
            }

            SdFile file;
            if (!file.open(&root, dirIndex, O_RDWR)) {
                root.close();
                DBG_PRINTLN("[DAV][DELETE] Index stale, falling back to name delete");
            } else {
                if (file.isDir()) {
                    file.close();
                    root.close();
                    sdcontrol.relinquishBusControl();
                    redirectToList();
                    return;
                }

                bool success = file.remove();
                file.close();
                root.close();

                if (success) {
                    DBG_PRINTLN("[DAV][DELETE] Delete successful by index");
                    sdcontrol.relinquishBusControl();
                    redirectToList();
                    return;
                } else {
                    DBG_PRINTLN("[DAV][DELETE] Delete by index failed, falling back to name delete");
                }
            }
        }

        String pathForOpen = SdFileUtils::toRelativePath(fullPath);

        DBG_PRINT("[DAV][DELETE] Path for delete: ");
        DBG_PRINTLN(pathForOpen);

        // Check if it's a directory
        SdFile testFile;
        bool isDirectory = false;
        if (SdFileUtils::openDirectory(sd, testFile, fullPath)) {
            isDirectory = true;
            testFile.close();
        } else if (SdFileUtils::openFileRead(sd, testFile, fullPath)) {
            isDirectory = testFile.isDir();
            testFile.close();
        }

        bool success = false;
        if (isDirectory) {
            DBG_PRINTLN("[DAV][DELETE] Removing directory");
            success = sd.rmdir(pathForOpen.c_str());
        } else {
            DBG_PRINTLN("[DAV][DELETE] Removing file");
            success = SdFileUtils::removeFile(sd, fullPath);
        }

        sdcontrol.relinquishBusControl();

        if (success) {
            DBG_PRINTLN("[DAV][DELETE] Delete successful");
        } else {
            DBG_PRINTLN("[DAV][DELETE] Delete failed");
        }
        redirectToList();
    }

    ESPWebDAV dav;
