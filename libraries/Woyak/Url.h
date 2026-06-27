#pragma once

#include <Arduino.h>

/// <summary>
/// Parses and provides access to URL components.
/// </summary>
/// <remarks>
/// Breaks a URL into its components: scheme, host, port, path, query, and fragment.
/// Automatically infers default ports based on scheme (443 for https/wss, 80 otherwise).
/// </remarks>
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
   /// <summary>
   /// Constructs a Url by parsing the specified URL string.
   /// </summary>
   /// <param name="url">A complete or partial URL string</param>
   explicit Url(String url)
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
         _port = (_scheme == "https" || _scheme == "wss") ? 443 : 80; // Default ports
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

   /// <summary>Gets the URL scheme (e.g., "http", "https", "ws", "wss").</summary>
   String getScheme() const { return _scheme; }

   /// <summary>Gets the hostname or IP address.</summary>
   String getHost() const { return _host; }

   /// <summary>Gets the port number.</summary>
   String getPort() const { return _port; }

   /// <summary>Gets the path component (always starts with '/').</summary>
   String getPath() const { return _path; }

   /// <summary>Gets the query string (without leading '?').</summary>
   String getQuery() const { return _query; }

   /// <summary>Gets the fragment identifier (without leading '#').</summary>
   String getFragment() const { return _fragment; }
};
