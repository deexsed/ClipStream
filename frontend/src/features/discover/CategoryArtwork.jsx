import { Gamepad2 } from "lucide-react";
import TwitchImage from "../../shared/ui/TwitchImage";

export default function CategoryArtwork({ game, size = 18 }) {
  return game.box_art ? (
    <TwitchImage src={game.box_art} alt="" loading="lazy" />
  ) : (
    <Gamepad2 size={size} aria-hidden="true" />
  );
}
