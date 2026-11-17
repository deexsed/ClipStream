import { forwardRef } from "react";
import { twitchImageUrl } from "../twitchImage";

export default forwardRef(function TwitchImage(
  { src, decoding = "async", ...props },
  ref,
) {
  return (
    <img {...props} ref={ref} src={twitchImageUrl(src)} decoding={decoding} />
  );
});
