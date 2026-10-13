using System.Net.Http.Headers;
using System.Text.Json;

namespace LittleBearPublisher;

/// <summary>
/// Talks to the Device Hub firmware API (same endpoints the Publish-*ToHub.ps1 scripts use).
/// </summary>
internal sealed class HubClient
{
    private readonly HttpClient _http = new() { Timeout = TimeSpan.FromMinutes(5) };

    public string Server { get; }

    public HubClient(string server, string? token)
    {
        Server = server.TrimEnd('/');
        if (!string.IsNullOrEmpty(token))
        {
            _http.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token);
        }
    }

    /// <summary>
    /// True when the server already has this version, and (if known) the same build time.
    /// Relies on the server's latest-version metadata already recorded on <paramref name="item"/>
    /// (via <see cref="GetLatestAsync"/>) rather than probing the download endpoint, since that
    /// endpoint does not reliably validate the requested version segment.
    /// </summary>
    public Task<bool> IsAlreadyPublishedAsync(FirmwareItem item)
    {
        if (item.ServerVersion == null)
        {
            // Server has no version info for this firmware; assume the known local
            // version is newer rather than treating it as already published.
            return Task.FromResult(false);
        }

        if (item.ServerVersion != item.Version)
        {
            return Task.FromResult(false);
        }

        bool alreadyPublished = item.BuildTime == null || item.ServerBuildTime == item.BuildTime;
        return Task.FromResult(alreadyPublished);
    }

    /// <summary>
    /// Gets the version and build time of the firmware currently published on the server, if any.
    /// </summary>
    /// <param name="firmware">The firmware name, e.g. "Sketch.Board".</param>
    /// <returns>The server's version and build time, or (null, null) if unavailable.</returns>
    public async Task<(string? Version, string? BuildTime)> GetLatestAsync(string firmware)
    {
        try
        {
            string json = await _http.GetStringAsync($"{Server}/api/firmware/{Uri.EscapeDataString(firmware)}/latest");
            using var doc = JsonDocument.Parse(json);
            string? version = doc.RootElement.TryGetProperty("version", out var v) ? v.GetString() : null;
            string? buildTime = doc.RootElement.TryGetProperty("buildTime", out var b) ? b.GetString() : null;
            return (version, buildTime);
        }
        catch
        {
            return (null, null);
        }
    }

    /// <summary>
    /// Uploads one firmware binary.
    /// </summary>
    /// <returns>Success flag and the server response or error text.</returns>
    public async Task<(bool Ok, string Detail)> UploadAsync(FirmwareItem item, string? notes)
    {
        try
        {
            byte[] bytes = await File.ReadAllBytesAsync(item.BinPath);
            using var form = new MultipartFormDataContent();
            form.Add(new StringContent(item.Version), "version");
            if (item.BuildTime != null)
            {
                form.Add(new StringContent(item.BuildTime), "buildTime");
            }
            if (!string.IsNullOrWhiteSpace(notes))
            {
                form.Add(new StringContent(notes), "notes");
            }

            var binary = new ByteArrayContent(bytes);
            binary.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
            form.Add(binary, "binary", Path.GetFileName(item.BinPath));

            using var response = await _http.PostAsync($"{Server}/api/firmware/{Uri.EscapeDataString(item.Firmware)}", form);
            string body = await response.Content.ReadAsStringAsync();
            return (response.IsSuccessStatusCode, response.IsSuccessStatusCode ? body : $"{(int)response.StatusCode}: {body}");
        }
        catch (Exception ex)
        {
            return (false, ex.Message);
        }
    }

    /// <summary>
    /// Deletes one published firmware version from the server.
    /// </summary>
    /// <param name="firmware">The firmware name, e.g. "Sketch.Board".</param>
    /// <param name="version">The version to delete.</param>
    /// <returns>Success flag and the server response or error text.</returns>
    public async Task<(bool Ok, string Detail)> DeleteAsync(string firmware, string version)
    {
        try
        {
            string url = $"{Server}/api/firmware/{Uri.EscapeDataString(firmware)}/{Uri.EscapeDataString(version)}";
            using var response = await _http.DeleteAsync(url);
            string body = await response.Content.ReadAsStringAsync();
            return (response.IsSuccessStatusCode, response.IsSuccessStatusCode ? body : $"{(int)response.StatusCode}: {body}");
        }
        catch (Exception ex)
        {
            return (false, ex.Message);
        }
    }
}
