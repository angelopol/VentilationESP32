using System;
using System.Net.Http;
using System.Threading.Tasks;

namespace Vento
{
    // API HTTP del ESP32 (/api/state, /api/mode, /api/setpoint)
    public sealed class HttpTransport : IDisposable
    {
        private readonly HttpClient _http;

        // address: "vento.local" o la IP de Vento
        public HttpTransport(string address)
        {
            _http = new HttpClient
            {
                BaseAddress = new Uri("http://" + address),
                Timeout = TimeSpan.FromSeconds(4),
            };
        }

        public async Task<VentoState> GetStateAsync() =>
            VentoState.Parse(await _http.GetStringAsync("/api/state"));

        public Task<VentoState> SetModeAsync(int mode) => PostAsync("/api/mode?v=" + mode);

        public Task<VentoState> SetSetpointAsync(int degrees) => PostAsync("/api/setpoint?v=" + degrees);

        private async Task<VentoState> PostAsync(string path)
        {
            using var r = await _http.PostAsync(path, null);
            r.EnsureSuccessStatusCode();
            return VentoState.Parse(await r.Content.ReadAsStringAsync());
        }

        public void Dispose() => _http.Dispose();
    }
}
