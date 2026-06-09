package grpc_server

import (
	"io"
	"net/http"
	"strings"
)

// Cloudflare trace: same request as latency test, no extra geoip.db needed.
const exitCountryPrefix = "__NKR_EXIT__|"

// FetchExitGeo returns exit IP and ISO country (loc=) from Cloudflare trace.
func FetchExitGeo(httpClient *http.Client) (ip, country string) {
	resp, err := httpClient.Get("https://www.cloudflare.com/cdn-cgi/trace")
	if err != nil {
		return "", ""
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(resp.Body)
	body := string(b)
	ip = strings.TrimSpace(getBetweenStr(body, "ip=", "\n"))
	loc := strings.ToUpper(strings.TrimSpace(getBetweenStr(body, "loc=", "\n")))
	if len(loc) == 2 {
		country = loc
	}
	return ip, country
}

// EncodeExitCountry passes ISO code to Qt via full_report (UrlTest only).
func EncodeExitCountry(country string) string {
	if country == "" {
		return ""
	}
	return exitCountryPrefix + country
}
