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
    /// </summary>
    public async Task<bool> IsAlreadyPublishedAsync(FirmwareItem item)
    {
        string name = Uri.EscapeDataString(item.Firmware);
        try
        {
            string downloadUrl = $"{Server}/api/firmware/{name}/{Uri.EscapeDataString(item.Version)}/download";
            using (var response = await _http.GetAsync(downloadUrl, HttpCompletionOption.ResponseHeadersRead))
            {
                if (response.StatusCode != System.Net.HttpStatusCode.OK)
                {
                    return false;
                }
            }

            if (item.BuildTime == null)
            {
                return true;
            }

            try
            {
                string json = await _http.GetStringAsync($"{Server}/api/firmware/{name}/latest");
                using var doc = JsonDocument.Parse(json);
                string? version = doc.RootElement.TryGetProperty("version", out var v) ? v.GetString() : null;
                string? buildTime = doc.RootElement.TryGetProperty("buildTime", out var b) ? b.GetString() : null;
                return !(version == item.Version && buildTime != item.BuildTime);
            }
            catch
            {
                return true;
            }
        }
        catch
        {
            return false;
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
}
