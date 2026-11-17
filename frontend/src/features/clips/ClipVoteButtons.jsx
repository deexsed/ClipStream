import { ThumbsDown, ThumbsUp } from "lucide-react";
import { Button } from "../../shared/ui/Primitives";
import { number } from "../../shared/format";

export default function ClipVoteButtons({ clip, vote, compact = false }) {
  return (
    <>
      {[
        [true, "like", "Нравится", "Лайки", clip.like_count, ThumbsUp],
        [
          false,
          "dislike",
          "Не нравится",
          "Дизлайки",
          clip.dislike_count,
          ThumbsDown,
        ],
      ].map(([isLike, value, label, countLabel, count, Icon]) => (
        <Button
          key={value}
          variant="plain"
          type="button"
          className={!compact && clip.user_vote === value ? "selected" : ""}
          title={compact ? label : undefined}
          aria-label={
            compact ? `${countLabel} в ClipStream: ${count || 0}` : label
          }
          aria-pressed={clip.user_vote === value}
          disabled={vote.isPending}
          onClick={() => vote.react(isLike)}
        >
          <Icon size={compact ? 14 : 17} />
          {number(count)}
        </Button>
      ))}
    </>
  );
}
