import ExternalLink from "../../shared/ui/ExternalLink";
import SectionNav from "../../shared/ui/SectionNav";
import { Surface } from "../../shared/ui/Layout";
import { EmptyState } from "../../shared/ui/Feedback";
import { useState } from "react";
import { useOutletContext, useSearchParams } from "react-router-dom";
import { ArrowUpRight } from "lucide-react";
import { useSession } from "../auth/Session";
import { ProfileList } from "./ProfilePage";
import { useApi, usePages, pageItems } from "../../shared/api/queries";
import { usePreferences } from "../../shared/preferences";
import {
  Identity,
  Button,
  PageHeading,
  LoadMore,
  Status,
} from "../../shared/ui/Primitives";
import ClipCard from "../clips/ClipCard";
import SearchField from "../../shared/ui/SearchField";
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
        <Surface>
          <p>Войдите через Twitch, чтобы собрать свою ленту.</p>
          <Button onClick={openLogin}>Войти через Twitch</Button>
        </Surface>
      ) : (
        <>
          <SectionNav
            label="Источники подписок"
            items={[
              ["feed", "Моя лента"],
              ["profiles", "Профили ClipStream"],
              ["twitch", "Подписки Twitch"],
            ]}
            value={tab}
            onChange={(key) => setParams({ tab: key })}
          />
          <div className="tab-content" key={tab}>
            {tab === "feed" ? (
              <FollowingFeed />
            ) : tab === "profiles" ? (
              <ProfileList />
            ) : (
              <TwitchFollowing />
            )}
          </div>
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
      <SearchField
        className="following-search"
        aria-label="Найти подписку Twitch"
        placeholder="Найти канал"
        value={search}
        onValueChange={setSearch}
      />
      <Status
        loading={query.isPending}
        error={query.error}
        onRetry={query.refetch}
      >
        <div className="people-list">
          {rows.map((channel) => (
            <div key={channel.broadcaster_id}>
              <Identity
                user={{
                  username: channel.broadcaster_name,
                  avatar_url: channel.profile_image_url || channel.avatar_url,
                }}
                to={`/p/twitch/${channel.broadcaster_id}`}
              >
                <strong>{channel.broadcaster_name}</strong>
                {channel.is_live && <span className="live-badge">В эфире</span>}
              </Identity>
              <ExternalLink
                className="text-link"
                href={`https://www.twitch.tv/${encodeURIComponent(channel.broadcaster_login)}`}
              >
                Twitch <ArrowUpRight size={14} />
              </ExternalLink>
            </div>
          ))}
        </div>
        {rows.length === 0 && (
          <EmptyState as="p">Каналов не найдено.</EmptyState>
        )}
      </Status>
    </section>
  );
}
