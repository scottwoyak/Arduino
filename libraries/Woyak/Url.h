#pragma once

#include <regex>

class Url
{
private:
   String _scheme;
   String _host;
   String _port;
   String _path;
   String _query;
   String _fragment;

public:
   // Constructor to parse a URL string
   Url(String url)
   {
      int protocolEnd = url.indexOf("://");
      if (protocolEnd != -1)
      {
         _scheme = url.substring(0, protocolEnd);
         url = url.substring(protocolEnd + 3);
      }

      int pathStart = url.indexOf('/');
      if (pathStart == -1)
      {
         pathStart = url.length(); // No path found, host is the rest of the string
      }

      String hostPort = url.substring(0, pathStart);
      url = url.substring(pathStart);

      int portStart = hostPort.indexOf(':');
      if (portStart != -1)
      {
         _host = hostPort.substring(0, portStart);
         _port = hostPort.substring(portStart + 1).toInt();
      }
      else
      {
         _host = hostPort;
         _port = (_scheme == "https" || _scheme== "wss") ? 443 : 80; // Default ports
      }

      int queryStart = url.indexOf('?');
      if (queryStart != -1)
      {
         _path = url.substring(0, queryStart);
         _query = url.substring(queryStart + 1);
      }
      else
      {
         _path = url;
      }

      if (_path.length() == 0)
      {
         _path = "/"; // Ensure path is at least '/'
      }
   }

   // Getter functions for URL components
   String getScheme() const { return _scheme; }
   String getHost() const { return _host; }
   String getPort() const { return _port; }
   String getPath() const { return _path; }
   String getQuery() const { return _query; }
   String getFragment() const { return _fragment; }
};
