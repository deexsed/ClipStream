import { useRecommendationFeed } from "../../shared/api/recommendations";
import Select from "../../shared/ui/Select";
import { useState } from "react";
import { Link, useParams, useSearchParams } from "react-router-dom";
import {
  ArrowUpRight,
  Gamepad2,
  Flame,
  Sparkles,
  Clock3,
  Grid2X2,
  LayoutGrid,
  Play,
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
import "./DiscoverPage.css";
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
  const [density, setDensity] = useState(() => {
    try {
      return localStorage.getItem("clipstream.discover.density") === "compact"
        ? "compact"
        : "comfortable";
    } catch {
      return "comfortable";
    }
  });
  const changeDensity = (value) => {
    setDensity(value);
    try {
      localStorage.setItem("clipstream.discover.density", value);
    } catch {}
  };
  const q = (params.get("q") || "").trim();
  const home = !mode && !q;
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
  const games = useApi(home ? null : "/games", { staleTime: 300000 });
  const personalGames = useApi(
    home && session.user ? "/games?personalized=1" : null,
    { queryKey: ["viewed-games", session.user?.id], staleTime: 0 },
  );
  const stripGames = home
    ? personalGames.data?.games || []
    : games.data?.games || [];
  const recommended =
    !mode &&
    !q &&
    !["browse", "tag", "sort", "period"].some((key) => params.has(key));
  const recommendations = useRecommendationFeed({
    viewer: session.user?.id,
    languages: languageKey,
    enabled: recommended,
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
    <div
      className={`page discover-page ${home ? "overview-page" : ""}`}
      data-density={density}
    >
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
      >
        {home && (
          <Link className="button overview-watch" to="/cliprush">
            <Play size={16} fill="currentColor" /> Открыть ClipRush{" "}
            <ArrowUpRight size={15} />
          </Link>
        )}
      </PageHeading>
      {!q && mode !== "categories" && (
        <Advertisement placement="banner" identity={mode || "home"} />
      )}
      {!q && mode !== "categories" && mode !== "category" && (
        <section
          className="overview-categories"
          aria-label={
            home ? "Категории из вашей истории" : "Популярные категории"
          }
        >
          {home && (
            <div className="overview-section-label">
              <span>Ваши категории</span>
              <span className="muted">На основе просмотренных клипов</span>
            </div>
          )}
          {home && personalGames.error && (
            <Status
              error={personalGames.error}
              onRetry={personalGames.refetch}
            />
          )}
          {home && session.user && personalGames.isPending && (
            <p className="muted">Загружаем категории из истории…</p>
          )}
          {home &&
            !personalGames.error &&
            (!session.user ||
              (!personalGames.isPending && !stripGames.length)) && (
              <p className="overview-category-hint muted">
                {session.user
                  ? "Посмотрите несколько клипов — здесь появятся ваши категории."
                  : "Войдите и смотрите клипы — здесь появятся категории из вашей истории."}
              </p>
            )}
          <div className="category-strip">
            {stripGames.slice(0, 10).map((game) => (
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
        </section>
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
          {home && (
            <nav className="overview-tabs" aria-label="Подборка клипов">
              {[
                ["recommended", "Для вас", Sparkles],
                ["popular", "Популярное", Flame],
                ["recent", "Новое", Clock3],
              ].map(([value, label, Icon]) => (
                <button
                  key={value}
                  aria-pressed={
                    value === "recommended"
                      ? recommended
                      : !recommended && sort === value
                  }
                  onClick={() =>
                    setParams((previous) => {
                      const next = new URLSearchParams(previous);
                      ["browse", "sort", "period", "tag"].forEach((key) =>
                        next.delete(key),
                      );
                      if (value !== "recommended") {
                        next.set("browse", "1");
                        next.set("sort", value);
                        next.set("period", "all");
                      }
                      return next;
                    })
                  }
                >
                  <Icon size={17} />
                  {label}
                </button>
              ))}
            </nav>
          )}
          <div className="filter-bar overview-filter-bar">
            <div className="overview-feed-heading">
              <h2>
                {recommended
                  ? "Моменты, которые стоит увидеть"
                  : home
                    ? sort === "recent"
                      ? "Свежее из эфира"
                      : "Клипы, которые смотрят"
                    : "Клипы"}
              </h2>
              {recommended && (
                <p className="muted">
                  Знакомые каналы и новые открытия в одной ленте
                </p>
              )}
              {!recommended && (
                <div className="segmented" aria-label="Период">
                  {periods.map(([value, label]) => (
                    <button
                      key={value}
                      aria-pressed={period === value}
                      onClick={() => setFilter({ period: value })}
                    >
                      {label}
                    </button>
                  ))}
                </div>
              )}
            </div>
            <div className="overview-feed-controls">
              <label className="select-label">
                <SlidersHorizontal size={15} />
                <Select
                  aria-label="Сортировка"
                  value={recommended ? "recommended" : sort}
                  onChange={(event) =>
                    setFilter({ sort: event.target.value, browse: "1" })
                  }
                >
                  {recommended && (
                    <option value="recommended">Рекомендации</option>
                  )}
                  <option value="popular">Популярные</option>
                  <option value="recent">Сначала новые</option>
                  <option value="duration">По длительности</option>
                </Select>
              </label>
              <div
                className="overview-density"
                role="group"
                aria-label="Плотность карточек"
              >
                {[
                  ["comfortable", "Крупные карточки", LayoutGrid],
                  ["compact", "Компактные карточки", Grid2X2],
                ].map(([value, label, Icon]) => (
                  <button
                    key={value}
                    aria-label={label}
                    title={label}
                    aria-pressed={density === value}
                    onClick={() => changeDensity(value)}
                  >
                    <Icon size={17} />
                  </button>
                ))}
              </div>
            </div>
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
            <div className="clips-grid overview-grid">
              {clips.map((clip, index) => (
                <ClipCard
                  key={clip.id}
                  clip={clip}
                  showStats
                  priority={index < 4}
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
              <button
                className="button"
                onClick={recommendations.startNewSession}
              >
                Новая подборка
              </button>
            )}
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
