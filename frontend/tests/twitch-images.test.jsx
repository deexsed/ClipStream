import React from "react";
import { render } from "@testing-library/react";
import { expect, it } from "vitest";
import { twitchImageUrl } from "../src/shared/twitchImage";
import { Avatar } from "../src/shared/ui/Primitives";

it.each([
  ["static-cdn.jtvnw.net", "/jtv_user_pictures/avatar-300x300.png"],
  ["static-cdn.jtvnw.net", "/ttv-boxart/123-285x380.jpg"],
  ["clips-media-assets.twitch.tv", "/preview.webp"],
  ["clips-media-assets2.twitch.tv", "/AT-cm%7Cpreview-480x272.jpg?size=1%2F2"],
])("uses the image cache for public assets on %s", (host, path) => {
  expect(twitchImageUrl(`https://${host}${path}#fragment`)).toBe(
    `/media/twitch/${host}${path}`,
  );
});

it.each([
  undefined,
  null,
  "",
  "/local.png",
  "not a URL",
  "data:image/png;base64,aA==",
  "https://other.example/avatar.png",
  "http://static-cdn.jtvnw.net/avatar.png",
  "https://static-cdn.jtvnw.net:444/avatar.png",
  "https://user:secret@static-cdn.jtvnw.net/avatar.png",
  "https://static-cdn.jtvnw.net.other.example/avatar.png",
  "https://static-cdn.jtvnw.net/image.svg",
  "https://static-cdn.jtvnw.net/movie.mp4",
  "https://clips.twitch.tv/embed?clip=hello",
])(
  "preserves unsupported sources without sending them through the proxy: %s",
  (value) => {
    expect(twitchImageUrl(value)).toBe(value);
  },
);

it("loads avatars lazily through the cache and permits eager loading where requested", () => {
  const user = { avatar_url: "https://static-cdn.jtvnw.net/avatar.png" };
  const view = render(<Avatar user={user} />);
  const image = view.container.querySelector("img");
  expect(image.getAttribute("src")).toBe(
    "/media/twitch/static-cdn.jtvnw.net/avatar.png",
  );
  expect(image.getAttribute("loading")).toBe("lazy");
  expect(image.getAttribute("decoding")).toBe("async");
  view.rerender(<Avatar user={user} loading="eager" />);
  expect(image.getAttribute("loading")).toBe("eager");
});
