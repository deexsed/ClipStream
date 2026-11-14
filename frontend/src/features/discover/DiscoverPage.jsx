import Select from "../../shared/ui/Select";
import { useRef } from "react";
import { Link, useParams, useSearchParams } from "react-router-dom";
import { useInfiniteQuery } from "@tanstack/react-query";
import {
  ArrowUpRight,
  Gamepad2,
  Search,
  SlidersHorizontal,
} from "lucide-react";
import { request, queryString } from "../../shared/api/http";
import { useApi, usePages, pageItems } from "../../shared/api/queries";
import { usePreferences } from "../../shared/preferences";
import { useSession } from "../auth/Session";
import {
  Avatar,
  LoadMore,
  PageHeading,
  Status,
} from "../../shared/ui/Primitives";
import ClipCard from "../clips/ClipCard";
import Advertisement from "../ads/Advertisement";
import { number, twitchProfilePath } from "../../shared/format";

const periods = [
  ["today", "Сегодня"],
  ["week", "Неделя"],
  ["month", "Месяц"],
  ["all", "Всё время"],
];
export default function DiscoverPage({ mode }) {
  const [params, setParams] = useSearchParams();
  const { id } = useParams();
  const { languageKey } = usePreferences();
  const session = useSession();
  const q = (params.get("q") || "").trim();
  const period = periods.some(([value]) => value === params.get("period"))
    ? params.get("period")
    : mode === "trends"
      ? "today"
      : "all";
  const sort = ["popular", "recent", "duration"].includes(params.get("sort"))
    ? params.get("sort")
    : "popular";
  const setFilter = (patch) =>
    setParams((previous) => {
      const next = new URLSearchParams(previous);
      Object.entries(patch).forEach(([key, value]) => next.set(key, value));
      return next;
    });
  const games = useApi("/games", { staleTime: 300000 });
  const recommended =
    !mode &&
    !q &&
    !["browse", "tag", "sort", "period"].some((key) => params.has(key));
  const sessionId = useRef(crypto.randomUUID());
  const recommendations = useInfiniteQuery({
    queryKey: ["recommendations", session.user?.id, languageKey],
    initialPageParam: "",
    enabled: recommended,
    queryFn: async ({ pageParam, signal }) => {
      const result = await request(
        `/clips/home-feed?${queryString({ limit: 24, session_id: sessionId.current, exclude: pageParam, clip_langs: languageKey })}`,
        { signal },
      );
      return {
        clips: (result.items || []).map((row) => row.clip).filter(Boolean),
        excluded: pageParam,
      };
    },
    getNextPageParam: (last, pages) => {
      const ids = [
        ...new Set(
          pages.flatMap((page) => page.clips.map((clip) => String(clip.id))),
        ),
      ];
      const old = new Set(last.excluded.split(","));
      return last.clips.length < 18 ||
        last.clips.every((clip) => old.has(String(clip.id))) ||
        ids.length >= 180
        ? undefined
        : ids.join(",");
    },
  });
  const feed = usePages(
    mode === "categories" || recommended ? null : "/clips",
    "clips",
    {
      game_id: mode === "category" ? id : undefined,
      period,
      sort,
      search: q || undefined,
      clip_langs: languageKey,
      tag: params.get("tag") || undefined,
    },
  );
  const result = recommended ? recommendations : feed;
  const clips = pageItems(result, "clips");
  const people = useApi(
    q.length >= 2 ? `/users/search?${queryString({ q, limit: 8 })}` : null,
  );
  const channels = useApi(
    q.length >= 2 ? `/channels/search?${queryString({ q, limit: 8 })}` : null,
    { staleTime: 60000 },
  );
  const category = games.data?.games?.find((game) => String(game.id) === id);
  const title = q
    ? `Результаты для «${q}»`
    : mode === "categories"
      ? "Найдите свою игру"
      : mode === "trends"
        ? "Сейчас в тренде"
        : mode === "category"
          ? category?.name || "Клипы категории"
          : "Лучшее из эфира";
  return (
    <div className="page discover-page">
      <PageHeading
        eyebrow={
          mode === "trends"
            ? "Самое обсуждаемое — прямо сейчас"
            : "Ваш ежедневный срез Twitch"
        }
        title={title}
        description={
          q
            ? "Клипы, люди и каналы в одном месте."
            : mode === "categories"
              ? "От больших турниров до маленьких открытий."
              : "Яркие моменты, новые открытия и любимые каналы."
        }
      />
      {!q && mode !== "categories" && (
        <Advertisement placement="banner" identity={mode || "home"} />
      )}
      {!q && mode !== "categories" && mode !== "category" && (
        <div className="category-strip" aria-label="Популярные категории">
          {(games.data?.games || []).slice(0, 7).map((game) => (
            <Link key={game.id} to={`/category/${game.id}`}>
              {game.box_art ? (
                <img src={game.box_art} alt="" />
              ) : (
                <Gamepad2 size={18} />
              )}
              <span>{game.name}</span>
              <ArrowUpRight size={13} />
            </Link>
          ))}
          <Link to="/categories" className="all-categories">
            Все категории <ArrowUpRight size={14} />
          </Link>
        </div>
      )}
      {mode === "categories" ? (
        <Status
          error={games.error}
          loading={games.isPending}
          empty={
            games.data?.games?.length === 0 && "Категории пока не появились"
          }
          onRetry={games.refetch}
        >
          <div className="categories-grid">
            {(games.data?.games || []).map((game) => (
              <Link
                className="category-card"
                to={`/category/${game.id}`}
                key={game.id}
              >
                <div>
                  {game.box_art ? (
                    <img src={game.box_art} alt="" loading="lazy" />
                  ) : (
                    <Gamepad2 size={40} />
                  )}
                </div>
                <h2>{game.name}</h2>
                <p>{number(game.clip_count)} клипов</p>
              </Link>
            ))}
          </div>
        </Status>
      ) : (
        <>
          <div className="filter-bar">
            <div className="segmented" aria-label="Период">
              {!recommended &&
                periods.map(([value, label]) => (
                  <button
                    key={value}
                    aria-pressed={period === value}
                    onClick={() => setFilter({ period: value })}
                  >
                    {label}
                  </button>
                ))}
              {recommended && (
                <>
                  <span className="section-title">Подобрано для вас</span>
                  <span className="subtle-badge">Клипы и открытия</span>
                </>
              )}
            </div>
            <label className="select-label">
              <SlidersHorizontal size={15} />
              <span className="sr-only">Сортировка</span>
              <Select
                aria-label="Сортировка"
                value={recommended ? "recommended" : sort}
                onChange={(event) => {
                  if (recommended) {
                    setParams((previous) => {
                      const next = new URLSearchParams(previous);
                      next.set("q", "");
                      next.set("sort", event.target.value);
                      next.set("period", "all");
                      next.set("browse", "1");
                      return next;
                    });
                  } else setFilter({ sort: event.target.value });
                }}
              >
                {recommended && (
                  <option value="recommended">Рекомендации</option>
                )}
                <option value="popular">Популярные</option>
                <option value="recent">Сначала новые</option>
                <option value="duration">По длительности</option>
              </Select>
            </label>
          </div>
          {params.get("tag") && (
            <p className="tag-filter">
              Тег: <strong>{params.get("tag")}</strong>
              <button
                className="text-link"
                onClick={() =>
                  setParams((previous) => {
                    const next = new URLSearchParams(previous);
                    next.delete("tag");
                    return next;
                  })
                }
              >
                Сбросить
              </button>
            </p>
          )}
          {q.length >= 2 && (
            <div className="search-people">
              <SearchGroup
                title="Люди"
                query={people}
                rows={people.data?.users}
                site
              />
              <SearchGroup
                title="Каналы Twitch"
                query={channels}
                rows={channels.data?.channels}
              />
            </div>
          )}
          <Status
            loading={result.isPending}
            error={!result.data && result.error}
            onRetry={result.refetch}
            empty={
              !result.isPending &&
              !result.error &&
              clips.length === 0 &&
              "По этим фильтрам клипов пока нет"
            }
          >
            <div
              className={`clips-grid ${recommended ? "recommended-grid" : ""}`}
            >
              {clips.map((clip, index) => (
                <ClipCard
                  key={clip.id}
                  clip={clip}
                  featured={recommended && index === 0}
                  rank={mode === "trends" ? index + 1 : undefined}
                />
              ))}
            </div>
            {result.error && !result.isFetchNextPageError && (
              <Status error={result.error} onRetry={result.refetch} />
            )}
            {clips.length >= 6 && (
              <Advertisement placement="feed" identity={mode || "home"} />
            )}
            <LoadMore query={result} />
            {recommended && !result.hasNextPage && clips.length > 0 && (
              <Link className="button" to="/?browse=1&period=all">
                Перейти ко всем клипам <ArrowUpRight size={15} />
              </Link>
            )}
          </Status>
          {session.user &&
            !result.isPending &&
            !result.error &&
            clips.length === 0 && (
              <p className="muted">
                Проверьте выбранные языки и{" "}
                <Link
                  className="text-link"
                  to={`/u/${encodeURIComponent(session.user.username)}?tab=settings`}
                >
                  настройки ленты
                </Link>
                .
              </p>
            )}
        </>
      )}
    </div>
  );
}

function SearchGroup({ title, query, rows = [], site }) {
  return (
    <section>
      <h2>{title}</h2>
      <Status
        error={query.error}
        loading={query.isPending}
        onRetry={query.refetch}
      >
        {rows.length === 0 ? (
          <p className="muted">Совпадений нет.</p>
        ) : (
          <div className="people-row">
            {rows.map((row) => (
              <Link
                key={row.id || row.broadcaster_id}
                to={
                  site
                    ? `/u/${encodeURIComponent(row.username)}`
                    : twitchProfilePath(
                        row.broadcaster_id,
                        row.broadcaster_login,
                      )
                }
              >
                <Avatar
                  user={{
                    ...row,
                    username: row.username || row.display_name,
                    avatar_url: row.avatar_url || row.thumbnail_url,
                  }}
                />
                <span>
                  {row.username || row.display_name || row.broadcaster_login}
                </span>
                {row.is_live && <span className="live-badge">В эфире</span>}
              </Link>
            ))}
          </div>
        )}
      </Status>
    </section>
  );
}
