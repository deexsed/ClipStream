import { useState } from "react";
import { Link, useOutletContext, useSearchParams } from "react-router-dom";
import { ArrowUpRight } from "lucide-react";
import { useSession } from "../auth/Session";
import { ProfileList } from "./ProfilePage";
import { useApi, usePages, pageItems } from "../../shared/api/queries";
import { usePreferences } from "../../shared/preferences";
import {
  Avatar,
  Button,
  PageHeading,
  LoadMore,
  Status,
} from "../../shared/ui/Primitives";
import ClipCard from "../clips/ClipCard";
export default function FollowingPage() {
  const session = useSession();
  const { openLogin } = useOutletContext();
  const [params, setParams] = useSearchParams();
  const tab = ["profiles", "twitch"].includes(params.get("tab"))
    ? params.get("tab")
    : "feed";
  return (
    <div className="page">
      <PageHeading
        eyebrow="Ваш круг"
        title="Отслеживаю"
        description="Каналы, авторы и их новые моменты."
      />
      {!session.user ? (
        <div className="surface">
          <p>Войдите через Twitch, чтобы собрать свою ленту.</p>
          <Button onClick={openLogin}>Войти через Twitch</Button>
        </div>
      ) : (
        <>
          <nav className="profile-tabs" aria-label="Источники подписок">
            {[
              ["feed", "Моя лента"],
              ["profiles", "Профили ClipStream"],
              ["twitch", "Подписки Twitch"],
            ].map(([key, label]) => (
              <button
                key={key}
                aria-current={tab === key ? "page" : undefined}
                onClick={() => setParams({ tab: key })}
              >
                {label}
              </button>
            ))}
          </nav>
          {tab === "feed" ? (
            <FollowingFeed />
          ) : tab === "profiles" ? (
            <ProfileList />
          ) : (
            <TwitchFollowing />
          )}
        </>
      )}
    </div>
  );
}
function FollowingFeed() {
  const { languageKey } = usePreferences();
  const query = usePages("/me/following/feed", "clips", {
    clip_langs: languageKey,
  });
  return (
    <section>
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
        empty={
          !query.isPending &&
          !query.error &&
          pageItems(query, "clips").length === 0 &&
          "Здесь появятся клипы отслеживаемых авторов"
        }
      >
        <div className="clips-grid">
          {pageItems(query, "clips").map((clip) => (
            <ClipCard key={clip.id} clip={clip} />
          ))}
        </div>
        <LoadMore query={query} />
      </Status>
    </section>
  );
}
function TwitchFollowing() {
  const query = useApi("/channels/followed", { staleTime: 60000 });
  const [search, setSearch] = useState("");
  const rows = (query.data?.channels || []).filter((channel) =>
    (channel.broadcaster_name + " " + channel.broadcaster_login)
      .toLowerCase()
      .includes(search.toLowerCase()),
  );
  return (
    <section>
      <p className="muted">
        Список из вашего Twitch. Изменение отслеживания в ClipStream не меняет
        подписки в Twitch.
      </p>
      <input
        className="following-search"
        type="search"
        aria-label="Найти подписку Twitch"
        placeholder="Найти канал"
        value={search}
        onChange={(event) => setSearch(event.target.value)}
      />
      <Status
        loading={query.isPending}
        error={query.error}
        onRetry={query.refetch}
      >
        <div className="people-list">
          {rows.map((channel) => (
            <div key={channel.broadcaster_id}>
              <Link
                className="creator"
                to={`/p/twitch/${channel.broadcaster_id}`}
              >
                <Avatar
                  user={{
                    username: channel.broadcaster_name,
                    avatar_url: channel.profile_image_url || channel.avatar_url,
                  }}
                />
                <strong>{channel.broadcaster_name}</strong>
                {channel.is_live && <span className="live-badge">В эфире</span>}
              </Link>
              <a
                className="text-link"
                href={`https://www.twitch.tv/${encodeURIComponent(channel.broadcaster_login)}`}
                target="_blank"
                rel="noopener noreferrer"
              >
                Twitch <ArrowUpRight size={14} />
              </a>
            </div>
          ))}
        </div>
        {rows.length === 0 && (
          <p className="empty-state">Каналов не найдено.</p>
        )}
      </Status>
    </section>
  );
}
