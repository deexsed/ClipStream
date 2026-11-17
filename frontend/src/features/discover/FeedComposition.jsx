import { useMemo } from "react";
import { ChartNoAxesColumnIncreasing } from "lucide-react";

export default function FeedComposition({ clips }) {
  const rows = useMemo(() => {
    const counts = new Map();
    for (const clip of clips) {
      const name = clip.game_name || "Без категории";
      counts.set(name, (counts.get(name) || 0) + 1);
    }
    const sorted = [...counts].sort((a, b) => b[1] - a[1]);
    if (sorted.length <= 5) return sorted;
    return [
      ...sorted.slice(0, 5),
      [
        "Остальные категории",
        sorted.slice(5).reduce((sum, [, count]) => sum + count, 0),
      ],
    ];
  }, [clips]);
  return (
    <details className="feed-composition">
      <summary>
        <ChartNoAxesColumnIncreasing size={15} /> Состав подборки{" "}
        <span className="muted">{clips.length} клипов загружено</span>
      </summary>
      <figure>
        <figcaption>
          Категории среди загруженных клипов. Это состав текущей подборки, а не
          статистика всего Twitch.
        </figcaption>
        <ul
          className="feed-composition-chart"
          aria-label="Количество загруженных клипов по категориям"
        >
          {rows.map(([name, count]) => (
            <li key={name}>
              <span title={name}>{name}</span>
              <span className="feed-composition-track" aria-hidden="true">
                <span style={{ width: `${(count / clips.length) * 100}%` }} />
              </span>
              <strong>{count}</strong>
            </li>
          ))}
        </ul>
      </figure>
    </details>
  );
}
