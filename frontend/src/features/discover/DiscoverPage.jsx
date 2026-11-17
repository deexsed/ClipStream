import CategoryPicker from "./CategoryPicker";
import { categoryIdsFrom, categoryParams } from "./categoryFilter";
import { recordCategoryUse } from "../../shared/categoryHistory";
import CategoryArtwork from "./CategoryArtwork";
import CategoryCatalog from "./CategoryCatalog";
import VirtualClipGrid from "./VirtualClipGrid";

import { Field } from "../../shared/ui/FormControls";
import { useRecommendationFeed } from "../../shared/api/recommendations";

import Select from "../../shared/ui/Select";
import { Link, useParams, useSearchParams } from "react-router-dom";
import { ArrowUpRight, Shuffle, X, Play } from "lucide-react";
import { queryString } from "../../shared/api/http";
import { useApi, usePages, pageItems } from "../../shared/api/queries";
import { usePreferences } from "../../shared/preferences";
import { useSession } from "../auth/Session";
import {
  Avatar,
  PageHeading,
  Status,
  Button,
  ButtonLink,
} from "../../shared/ui/Primitives";
import ClipCard from "../clips/ClipCard";
import Advertisement from "../ads/Advertisement";
import "./DiscoverPage.css";
import FeedComposition from "./FeedComposition";
import { twitchProfilePath } from "../../shared/format";

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
  const gameIds = mode === "category" ? [id] : categoryIdsFrom(params);
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
  const setFilter = (patch) => {
    setParams((previous) => {
      const next = new URLSearchParams(previous);
      Object.entries(patch).forEach(([key, value]) =>
        value ? next.set(key, value) : next.delete(key),
      );
      return next;
    });
  };
  const changeCategories = (ids) => {
    if (
      ids.length === gameIds.length &&
      ids.every((id) => gameIds.includes(id))
    )
      return;
    ids
      .filter((id) => !gameIds.includes(id))
      .forEach((id) => recordCategoryUse(id, session.user?.id));
    setFilter({
      game_id: "",
      game_ids: "",
      ...categoryParams(ids),
      browse: "1",
    });
  };
  const games = useApi("/games", { staleTime: 300000 });
  const personalGames = useApi(session.user ? "/games?personalized=1" : null, {
    queryKey: ["viewed-games", session.user?.id],
    staleTime: 0,
  });
  const stripGames = home
    ? personalGames.data?.games || []
    : games.data?.games || [];
  const recommended =
    !mode &&
    !q &&
    !["browse", "tag", "sort", "period", "game_id", "game_ids"].some((key) =>
      params.has(key),
    );
  const recommendations = useRecommendationFeed({
    viewer: session.user?.id,
    languages: languageKey,
    enabled: recommended,
  });
  const feed = usePages(
    mode === "categories" || recommended ? null : "/clips",
    "clips",
    {
      ...categoryParams(gameIds),
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
          : session.user
            ? "Ваша лента"
            : "Клипы";
  return (
    <div className={`page discover-page ${home ? "overview-page" : ""}`}>
      <PageHeading
        eyebrow={
          home
            ? undefined
            : mode === "trends"
              ? "Самое обсуждаемое — прямо сейчас"
              : "ClipStream / Главная"
        }
        title={title}
        description={
          home
            ? undefined
            : q
              ? "Клипы, люди и каналы в одном месте."
              : mode === "categories"
                ? "От больших турниров до маленьких открытий."
                : "Любимые каналы, свежие клипы и неожиданные открытия — вместе."
        }
      >
        {home && (
          <ButtonLink className="overview-watch" to="/cliprush">
            <Play size={16} fill="currentColor" /> ClipRush
          </ButtonLink>
        )}
      </PageHeading>
      {!q && mode !== "categories" && (
        <Advertisement placement="banner" identity={mode || "home"} />
      )}
      {!q &&
        mode !== "categories" &&
        mode !== "category" &&
        (!home ||
          (session.user && (stripGames.length > 0 || personalGames.error))) && (
          <section
            className={`overview-categories ${!stripGames.length ? "overview-categories-empty" : ""}`}
            aria-label={
              home ? "Категории из вашей истории" : "Популярные категории"
            }
          >
            {home && stripGames.length > 0 && (
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
            <div className="category-strip">
              {stripGames.slice(0, 10).map((game) =>
                home ? (
                  <Button
                    variant="plain"
                    key={game.id}
                    className="category-chip"
                    aria-pressed={gameIds.includes(String(game.id))}
                    onClick={() =>
                      changeCategories(
                        gameIds.includes(String(game.id))
                          ? gameIds.filter((id) => id !== String(game.id))
                          : [String(game.id)],
                      )
                    }
                  >
                    <CategoryArtwork game={game} />
                    <span>{game.name}</span>
                  </Button>
                ) : (
                  <Link key={game.id} to={`/category/${game.id}`}>
                    <CategoryArtwork game={game} />
                    <span>{game.name}</span>
                    <ArrowUpRight size={13} />
                  </Link>
                ),
              )}
              {home ? (
                <Button
                  variant="plain"
                  className="category-chip all-categories"
                  aria-pressed={!gameIds.length}
                  onClick={() => changeCategories([])}
                >
                  Все категории
                </Button>
              ) : (
                <Link to="/categories" className="all-categories">
                  Все категории <ArrowUpRight size={14} />
                </Link>
              )}
            </div>
          </section>
        )}
      {mode === "categories" ? (
        <CategoryCatalog query={games} />
      ) : (
        <>
          <section className="feed-toolbar" aria-label="Управление лентой">
            <div className="feed-filter-controls">
              <Field inline className="select-label">
                <Select
                  aria-label="Сортировка"
                  value={recommended ? "recommended" : sort}
                  onChange={(event) => {
                    if (event.target.value === "recommended") {
                      setParams((previous) => {
                        const next = new URLSearchParams(previous);
                        [
                          "browse",
                          "sort",
                          "period",
                          "tag",
                          "game_id",
                          "game_ids",
                        ].forEach((key) => next.delete(key));
                        return next;
                      });
                    } else setFilter({ sort: event.target.value, browse: "1" });
                  }}
                >
                  {home && <option value="recommended">Смешанная лента</option>}
                  <option value="popular">Популярные</option>
                  <option value="recent">Сначала новые</option>
                  <option value="duration">По длительности</option>
                </Select>
              </Field>{" "}
              {mode !== "category" && (
                <div className="feed-filter">
                  <CategoryPicker
                    value={gameIds}
                    games={games.data?.games || []}
                    viewed={personalGames.data?.games || []}
                    viewer={session.user?.id}
                    loading={games.isPending}
                    error={games.error}
                    onRetry={games.refetch}
                    onChange={changeCategories}
                  />
                </div>
              )}
              <Field inline className="feed-filter">
                <span>Период</span>
                <Select
                  aria-label="Период клипов"
                  value={period}
                  onChange={(event) =>
                    setFilter({ period: event.target.value, browse: "1" })
                  }
                >
                  {periods.map(([value, label]) => (
                    <option key={value} value={value}>
                      {label}
                    </option>
                  ))}
                </Select>
              </Field>
            </div>
            <div className="feed-toolbar-actions">
              {home && recommended && (
                <Button
                  className="feed-shuffle"
                  disabled={result.isFetching}
                  busy={result.isFetching}
                  onClick={recommendations.startNewSession}
                >
                  <Shuffle size={16} /> Новая смесь
                </Button>
              )}
              {!recommended && (
                <Button
                  variant="plain"
                  className="text-link"
                  onClick={() =>
                    setParams((previous) => {
                      const next = new URLSearchParams(previous);
                      [
                        "browse",
                        "sort",
                        "period",
                        "tag",
                        "game_id",
                        "game_ids",
                      ].forEach((key) => next.delete(key));
                      return next;
                    })
                  }
                >
                  <X size={14} /> Сбросить фильтры
                </Button>
              )}
            </div>
            {games.error && (
              <Status error={games.error} onRetry={games.refetch} />
            )}
          </section>
          {params.get("tag") && (
            <p className="tag-filter">
              Тег: <strong>{params.get("tag")}</strong>
              <Button
                variant="plain"
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
              </Button>
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
            {home && clips.length > 0 && <FeedComposition clips={clips} />}
            <VirtualClipGrid
              key={JSON.stringify([
                mode,
                id,
                params.toString(),
                languageKey,
                session.user?.id,
              ])}
              clips={clips}
              query={result}
              renderClip={(clip, index) => (
                <ClipCard
                  key={clip.id}
                  clip={clip}
                  showStats
                  priority={index < 4}
                  onCategorySelect={
                    home
                      ? (gameId) => changeCategories([String(gameId)])
                      : undefined
                  }
                  rank={mode === "trends" ? index + 1 : undefined}
                />
              )}
            />
            {result.error && !result.isFetchNextPageError && (
              <Status error={result.error} onRetry={result.refetch} />
            )}
            {clips.length >= 6 && (
              <Advertisement placement="feed" identity={mode || "home"} />
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
