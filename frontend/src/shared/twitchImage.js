const imageHosts = new Set([
  "static-cdn.jtvnw.net",
  "clips-media-assets.twitch.tv",
  "clips-media-assets2.twitch.tv",
]);

// Public raster assets only. Player URLs and authenticated API calls stay separate.
export function twitchImageUrl(value) {
  if (typeof value !== "string" || !value) return value;
  try {
    const url = new URL(value);
    if (
      url.protocol !== "https:" ||
      url.username ||
      url.password ||
      url.port ||
      !imageHosts.has(url.hostname) ||
      !/\.(jpe?g|png|webp|gif|avif)$/i.test(url.pathname)
    )
      return value;
    return `/media/twitch/${url.hostname}${url.pathname}${url.search}`;
  } catch {
    return value;
  }
}
