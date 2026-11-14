import Select from "../../shared/ui/Select";
import { useEffect, useState } from "react";
import {
  Link,
  useLocation,
  useNavigate,
  useOutletContext,
  useParams,
  useSearchParams,
} from "react-router-dom";
import { useMutation } from "@tanstack/react-query";
import {
  ArrowUpRight,
  Award,
  Check,
  LogOut,
  UserPlus,
  Users,
} from "lucide-react";
import { request, queryString } from "../../shared/api/http";
import {
  useApi,
  usePages,
  pageItems,
  queryClient,
} from "../../shared/api/queries";
import { usePreferences } from "../../shared/preferences";
import {
  Avatar,
  Button,
  Dialog,
  LoadMore,
  PageHeading,
  Status,
} from "../../shared/ui/Primitives";
import { date, number } from "../../shared/format";
import { useSession } from "../auth/Session";
import ClipCard from "../clips/ClipCard";

export default function ProfilePage({ source = "user" }) {
  const { username, login, id } = useParams();
  const [params, setParams] = useSearchParams();
  const location = useLocation();
  const navigate = useNavigate();
  const session = useSession();
  const { openLogin } = useOutletContext();
  const path =
    source === "twitch"
      ? `/profiles/twitch/${encodeURIComponent(id)}`
      : source === "login"
        ? `/profiles/twitch-login/${encodeURIComponent(login)}`
        : `/profiles/users/${encodeURIComponent(username)}`;
  const query = useApi(path);
  const profile = query.data?.profile;
  const self = profile?.relationship?.self;
  const local = profile?.local_user;
  const twitch = profile?.twitch;
  const visible = !profile?.private;
  useEffect(() => {
    if (profile?.canonical_path && location.pathname !== profile.canonical_path)
      navigate(profile.canonical_path + location.search, { replace: true });
  }, [profile?.canonical_path, location.pathname, location.search, navigate]);
  const tabs = [
    ["overview", "Обзор"],
    ["clips", "Клипы"],
    ...(local
      ? [
          ["wall", "Записи"],
          ["friends", "Друзья"],
        ]
      : []),
    ...(profile?.capabilities?.can_view_followers
      ? [["followers", "Подписчики"]]
      : []),
    ...(profile?.capabilities?.can_view_following
      ? [["following", "Отслеживает"]]
      : []),
    ...(self
      ? [
          ["activity", "История"],
          ["friends_feed", "Друзья смотрят"],
          ["settings", "Настройки"],
        ]
      : []),
  ];
  const tab = tabs.some(([key]) => key === params.get("tab"))
    ? params.get("tab")
    : "overview";
  return (
    <div className="page profile-page">
      <Status
        loading={query.isPending}
        error={query.error}
        onRetry={query.refetch}
      >
        {profile && (
          <>
            <header className="profile-header">
              <Avatar
                user={{
                  ...profile,
                  username: profile.display_name || local?.username,
                }}
                size="avatar-large"
              />
              <div>
                <p className="eyebrow">
                  {twitch?.is_live ? "В эфире на Twitch" : "Профиль ClipStream"}
                </p>
                <h1>
                  {profile.about?.display_name ||
                    twitch?.display_name ||
                    profile.display_name ||
                    local?.username}
                </h1>
                {twitch?.login && <p className="muted">@{twitch.login}</p>}
                {visible && (
                  <p className="profile-bio muted">
                    {profile.about?.bio || twitch?.description}
                  </p>
                )}
                {!local && (
                  <small>Этот канал ещё не присоединился к ClipStream.</small>
                )}
              </div>
            </header>
            <div className="profile-actions">
              <FollowButton profile={profile} />
              {local && !self && (
                <FriendActions
                  user={{
                    ...local,
                    friendship:
                      {
                        pending_incoming: "incoming",
                        pending_outgoing: "outgoing",
                      }[profile.relationship?.friendship] ||
                      profile.relationship?.friendship,
                  }}
                />
              )}
              {twitch?.login && (
                <a
                  className="button"
                  href={`https://www.twitch.tv/${encodeURIComponent(twitch.login)}`}
                  target="_blank"
                  rel="noopener noreferrer"
                >
                  Открыть Twitch <ArrowUpRight size={15} />
                </a>
              )}
              {self && (
                <Button busy={session.loggingOut} onClick={session.logout}>
                  <LogOut size={15} /> Выйти
                </Button>
              )}
            </div>
            {self && session.logoutError && (
              <Status error={session.logoutError} onRetry={session.logout} />
            )}
            {!visible ? (
              <div className="surface">
                <h2>Закрытый профиль</h2>
                <p className="muted">
                  Владелец ограничил доступ к содержимому профиля.
                </p>
              </div>
            ) : (
              <>
                <nav className="profile-tabs" aria-label="Разделы профиля">
                  {tabs.map(([key, label]) => (
                    <button
                      key={key}
                      aria-current={tab === key ? "page" : undefined}
                      onClick={() =>
                        setParams((previous) => {
                          const next = new URLSearchParams(previous);
                          next.set("tab", key);
                          return next;
                        })
                      }
                    >
                      {label}
                    </button>
                  ))}
                </nav>
                {tab === "overview" && (
                  <>
                    <div className="profile-stats">
                      {[
                        ["followers", "Подписчиков"],
                        ["following", "Отслеживает"],
                        ["friends", "Друзей"],
                        ["clips", "Клипов"],
                      ]
                        .filter(([key]) => profile.stats?.[key] != null)
                        .map(([key, label]) => (
                          <div key={key}>
                            <strong>{number(profile.stats[key])}</strong>
                            <span>{label}</span>
                          </div>
                        ))}
                    </div>
                    {twitch?.is_live && (
                      <section className="surface channel-status">
                        <span className="live-badge">Прямой эфир</span>
                        <h2>{twitch.stream?.title || twitch.channel?.title}</h2>
                        <p className="muted">
                          {twitch.stream?.game_name ||
                            twitch.channel?.game_name}
                          {twitch.stream?.viewer_count != null &&
                            ` · ${number(twitch.stream.viewer_count)} зрителей`}
                        </p>
                        <a
                          className="text-link"
                          href={`https://www.twitch.tv/${encodeURIComponent(twitch.login)}`}
                          target="_blank"
                          rel="noopener noreferrer"
                        >
                          Смотреть эфир <ArrowUpRight size={15} />
                        </a>
                      </section>
                    )}
                    {profile.about?.website && (
                      <a
                        className="text-link"
                        href={profile.about.website}
                        target="_blank"
                        rel="noopener noreferrer ugc"
                      >
                        Сайт автора <ArrowUpRight size={14} />
                      </a>
                    )}
                    <UnifiedClips profile={profile} />
                  </>
                )}
                {tab === "clips" && <UnifiedClips profile={profile} />}
                {tab === "wall" && local && (
                  <Wall
                    username={local.username}
                    self={self}
                    canWrite={profile.capabilities?.can_write_wall}
                  />
                )}
                {tab === "friends" && local && (
                  <Friends userId={local.id} self={self} />
                )}
                {["followers", "following"].includes(tab) && (
                  <ProfileList
                    key={tab}
                    path={`/profiles/${profile.key}/${tab}`}
                    title={
                      tab === "followers"
                        ? "Подписчики в ClipStream"
                        : "Отслеживает в ClipStream"
                    }
                  />
                )}
                {tab === "activity" && self && <Activity userId={local.id} />}
                {tab === "friends_feed" && self && <ProfileClips friends />}
                {tab === "settings" && self && (
                  <>
                    <ProfileSettings />
                    <Settings />
                  </>
                )}
              </>
            )}
          </>
        )}
      </Status>
    </div>
  );
}
export function FollowButton({ profile }) {
  const session = useSession();
  const { openLogin } = useOutletContext();
  const follows = profile.relationship?.following;
  const change = useMutation({
    mutationFn: () =>
      request(`/profiles/${profile.key}/follow`, {
        method: follows ? "DELETE" : "POST",
      }),
    onSuccess: refreshSocial,
  });
  if (profile.relationship?.self) return null;
  return (
    <div>
      <Button
        variant={follows ? "secondary" : "primary"}
        busy={change.isPending}
        onClick={() => (session.user ? change.mutate() : openLogin())}
      >
        {follows ? <Check size={15} /> : <UserPlus size={15} />}
        {follows ? "Отслеживается" : "Отслеживать"}
      </Button>
      {change.error && <Status error={change.error} />}
    </div>
  );
}
export function ProfileList({ path = "/me/following", title = "Отслеживаю" }) {
  const query = usePages(path, "profiles", {}, { pageSize: 24 });
  return (
    <section>
      <h2>{title}</h2>
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
      >
        {pageItems(query, "profiles", "key").length === 0 && (
          <p className="empty-state">Список пока пуст.</p>
        )}
        <div className="people-list">
          {pageItems(query, "profiles", "key").map((profile) => (
            <div key={profile.key}>
              <Link className="creator" to={profile.canonical_path}>
                <Avatar user={{ ...profile, username: profile.display_name }} />
                <strong>
                  {profile.display_name ||
                    profile.local_user?.username ||
                    profile.twitch?.login}
                </strong>
              </Link>
              <FollowButton profile={profile} />
            </div>
          ))}
        </div>
        <LoadMore query={query} />
      </Status>
    </section>
  );
}
function UnifiedClips({ profile }) {
  const { languageKey } = usePreferences();
  const [scope, setScope] = useState("all");
  const query = usePages(`/profiles/${profile.key}/clips`, "clips", {
    clip_langs: languageKey,
    scope,
  });
  const rows = pageItems(query, "clips");
  return (
    <section>
      <div className="section-label">
        <h2>Клипы</h2>
        <label className="select-label">
          <span className="sr-only">Какие клипы показывать</span>
          <Select
            aria-label="Какие клипы показывать"
            value={scope}
            onChange={(event) => setScope(event.target.value)}
          >
            <option value="all">Все связанные клипы</option>
            {profile.twitch && <option value="channel">С этого канала</option>}
            <option value="created">Созданные автором</option>
          </Select>
        </label>
      </div>
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
        empty={
          !query.isPending &&
          !query.error &&
          rows.length === 0 &&
          "Клипов пока нет"
        }
      >
        <div className="clips-grid">
          {rows.map((clip) => (
            <ClipCard key={clip.id} clip={clip} />
          ))}
        </div>
        <LoadMore query={query} />
      </Status>
    </section>
  );
}
function ProfileSettings() {
  const query = useApi("/me/profile/settings");
  return (
    <section className="narrow">
      <h2>Профиль и приватность</h2>
      <Status
        loading={query.isPending}
        error={query.error}
        onRetry={query.refetch}
      >
        {query.data && <ProfileSettingsForm initial={query.data} />}
      </Status>
    </section>
  );
}
function ProfileSettingsForm({ initial }) {
  const save = useMutation({
    mutationFn: (body) =>
      request("/me/profile/settings", { method: "PUT", body }),
    onSuccess: refreshSocial,
  });
  return (
    <form
      className="surface form-stack"
      onSubmit={(event) => {
        event.preventDefault();
        save.mutate(Object.fromEntries(new FormData(event.currentTarget)));
      }}
    >
      <label>
        Отображаемое имя
        <input
          name="display_name"
          defaultValue={initial.display_name}
          maxLength={64}
        />
      </label>
      <small>
        Ник и привязка Twitch останутся подтверждёнными аккаунтом Twitch.
      </small>
      <label>
        О себе
        <textarea name="bio" defaultValue={initial.bio} maxLength={1000} />
      </label>
      <label>
        Сайт
        <input
          name="website"
          type="url"
          defaultValue={initial.website}
          maxLength={512}
        />
      </label>
      {[
        ["profile_visibility", "Кому виден профиль"],
        ["followers_visibility", "Кому видны подписчики"],
        ["following_visibility", "Кому виден список отслеживания"],
      ].map(([key, label]) => (
        <label key={key}>
          {label}
          <Select aria-label={label} name={key} defaultValue={initial[key]}>
            <option value="public">Всем</option>
            <option value="friends">Друзьям</option>
            <option value="private">Только мне</option>
          </Select>
        </label>
      ))}
      <label>
        Кто может оставлять записи
        <Select
          aria-label="Кто может оставлять записи"
          name="wall_policy"
          defaultValue={initial.wall_policy}
        >
          <option value="everyone">Все пользователи</option>
          <option value="friends">Друзья</option>
          <option value="none">Только я</option>
        </Select>
      </label>
      {save.error && <Status error={save.error} />}
      {save.isSuccess && (
        <p className="feedback" role="status">
          Профиль сохранён
        </p>
      )}
      <Button variant="primary" busy={save.isPending}>
        Сохранить профиль
      </Button>
    </form>
  );
}
function refreshSocial() {
  return queryClient.invalidateQueries({
    predicate: (query) =>
      typeof query.queryKey[1] === "string" &&
      /(friends|profiles|following|users\/public|wall)/.test(query.queryKey[1]),
  });
}

function FriendActions({ user }) {
  const session = useSession();
  const { openLogin } = useOutletContext();
  const status = user.friendship || "none";
  const mutation = useMutation({
    mutationFn: (action) =>
      request(
        `/social/friends/${action === "remove" ? "" : action === "cancel" ? "request/" : action + "/"}${user.id}`,
        {
          method: ["remove", "cancel"].includes(action) ? "DELETE" : "POST",
          body: ["remove", "cancel"].includes(action) ? undefined : {},
        },
      ),
    onSuccess: refreshSocial,
  });
  if (session.user?.id === user.id) return null;
  return (
    <div className="friend-actions">
      <div className="button-row">
        {status === "none" && (
          <Button
            busy={mutation.isPending}
            onClick={() =>
              session.user ? mutation.mutate("request") : openLogin()
            }
          >
            <UserPlus size={16} /> Добавить в друзья
          </Button>
        )}
        {status === "outgoing" && (
          <Button
            busy={mutation.isPending}
            onClick={() => mutation.mutate("cancel")}
          >
            Отменить заявку
          </Button>
        )}
        {status === "friends" && (
          <Button
            busy={mutation.isPending}
            onClick={() => mutation.mutate("remove")}
          >
            <Check size={16} /> Удалить из друзей
          </Button>
        )}
        {status === "incoming" && (
          <>
            <Button
              variant="primary"
              busy={mutation.isPending}
              onClick={() => mutation.mutate("accept")}
            >
              Принять заявку
            </Button>
            <Button
              busy={mutation.isPending}
              onClick={() => mutation.mutate("decline")}
            >
              Отклонить
            </Button>
          </>
        )}
      </div>
      {mutation.error && <Status error={mutation.error} />}
    </div>
  );
}

function Wall({ username, self, canWrite = self }) {
  const [body, setBody] = useState("");
  const path = `/users/${encodeURIComponent(username)}/wall`;
  const query = usePages(path, "posts", {}, { pageSize: 20 });
  const rows = pageItems(query, "posts");
  const mutation = useMutation({
    mutationFn: () =>
      request("/social/wall", {
        method: "POST",
        body: { username, body: body.trim() },
      }),
    onSuccess: () => {
      setBody("");
      queryClient.invalidateQueries({ queryKey: ["pages", path] });
    },
  });
  return (
    <section className="wall">
      <h2>Записи</h2>
      {canWrite && (
        <form
          className="surface form-stack"
          onSubmit={(event) => {
            event.preventDefault();
            if (!mutation.isPending) mutation.mutate();
          }}
        >
          <label>
            <span className="sr-only">Новая запись</span>
            <textarea
              value={body}
              onChange={(event) => setBody(event.target.value)}
              required
              maxLength={4000}
              placeholder="Поделитесь моментом. Упоминания: @ник или @clip/ID"
            />
          </label>
          <div className="button-row end">
            <Button
              variant="primary"
              disabled={!body.trim()}
              busy={mutation.isPending}
            >
              Опубликовать
            </Button>
          </div>
          {mutation.error && <Status error={mutation.error} />}
        </form>
      )}
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
      >
        {rows.length === 0 && <p className="empty-state">Записей пока нет.</p>}
        {rows.map((post) => (
          <WallPost
            key={post.id}
            post={post}
            owner={self}
            refresh={() =>
              queryClient.invalidateQueries({ queryKey: ["pages", path] })
            }
          />
        ))}
        <LoadMore query={query} />
      </Status>
    </section>
  );
}

function WallPost({ post, owner, refresh }) {
  const session = useSession();
  const [editing, setEditing] = useState(false);
  const [deleting, setDeleting] = useState(false);
  const [body, setBody] = useState(post.body);
  const author = session.user?.id === post.author?.id;
  const canDelete = author || owner || [1, 2].includes(session.user?.role);
  const mutation = useMutation({
    mutationFn: (method) =>
      request(`/social/wall/${post.id}`, {
        method,
        body: method === "PUT" ? { body: body.trim() } : undefined,
      }),
    onSuccess: () => {
      setEditing(false);
      setDeleting(false);
      refresh();
    },
  });
  return (
    <article className="wall-post surface">
      <div className="section-label">
        <Link
          className="creator"
          to={`/u/${encodeURIComponent(post.author?.username || "")}`}
        >
          <Avatar user={post.author} />
          <div>
            <strong>{post.author?.username || "Пользователь"}</strong>
            <small>{date(post.created_at)}</small>
          </div>
        </Link>
        <div className="button-row">
          {author && (
            <Button
              onClick={() => {
                setBody(post.body);
                setEditing(!editing);
              }}
            >
              {editing ? "Отмена" : "Изменить"}
            </Button>
          )}
          {canDelete && (
            <Button onClick={() => setDeleting(true)}>Удалить</Button>
          )}
        </div>
      </div>
      {editing ? (
        <form
          className="form-stack"
          onSubmit={(event) => {
            event.preventDefault();
            mutation.mutate("PUT");
          }}
        >
          <label>
            Текст записи
            <textarea
              value={body}
              onChange={(event) => setBody(event.target.value)}
              required
              maxLength={4000}
            />
          </label>
          <small>
            Редактирование упоминаний не отправляет повторных уведомлений.
          </small>
          <Button disabled={!body.trim()} busy={mutation.isPending}>
            Сохранить
          </Button>
        </form>
      ) : (
        <p className="post-body">
          <MentionText text={post.body} />
        </p>
      )}
      {mutation.error && !deleting && <Status error={mutation.error} />}
      <Dialog
        open={deleting}
        onClose={() => setDeleting(false)}
        title="Удалить запись"
      >
        <p>Запись будет удалена из профиля вместе с упоминаниями.</p>
        {mutation.error && <Status error={mutation.error} />}
        <div className="dialog-actions">
          <Button onClick={() => setDeleting(false)}>Отмена</Button>
          <Button
            variant="primary"
            busy={mutation.isPending}
            onClick={() => mutation.mutate("DELETE")}
          >
            Удалить запись
          </Button>
        </div>
      </Dialog>
    </article>
  );
}
function MentionText({ text }) {
  return String(text || "")
    .split(/(@clip\/\d+|@[\p{L}\p{N}_-]+)/gu)
    .map((part, index) =>
      part.startsWith("@clip/") ? (
        <Link key={index} to={`/clip/${part.slice(6)}`}>
          {part}
        </Link>
      ) : part.startsWith("@") ? (
        <Link key={index} to={`/u/${encodeURIComponent(part.slice(1))}`}>
          {part}
        </Link>
      ) : (
        part
      ),
    );
}

function ProfileClips({ userId, friends }) {
  const { languageKey } = usePreferences();
  const query = usePages(
    friends ? "/social/friends-feed" : `/social/users/${userId}/clips`,
    "clips",
    { clip_langs: languageKey },
  );
  const rows = pageItems(query, "clips");
  return (
    <section>
      <h2>{friends ? "Друзья смотрят" : "Клипы"}</h2>
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
        empty={
          rows.length === 0 &&
          !query.isPending &&
          !query.error &&
          "Клипов пока нет"
        }
      >
        <div className="clips-grid">
          {rows.map((clip) => (
            <ClipCard key={clip.id} clip={clip} />
          ))}
        </div>
        <LoadMore query={query} />
      </Status>
    </section>
  );
}

function Friends({ userId, self }) {
  return (
    <div className="friends-sections">
      {self && (
        <>
          <PeopleList
            path="/social/friends/incoming"
            title="Входящие заявки"
            friendship="incoming"
          />
          <PeopleList
            path="/social/friends/outgoing"
            title="Отправленные заявки"
            friendship="outgoing"
          />
        </>
      )}
      <PeopleList
        path={`/social/users/${userId}/friends`}
        title="Друзья"
        friendship={self ? "friends" : undefined}
      />
    </div>
  );
}

function PeopleList({ path, title, friendship }) {
  const query = usePages(path, "users", {}, { pageSize: 30 });
  const rows = pageItems(query, "users");
  return (
    <section>
      <h2>{title}</h2>
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
      >
        {rows.length === 0 && <p className="muted">Здесь пока никого.</p>}
        <div className="people-list">
          {rows.map((user) => (
            <div key={user.id}>
              <Link
                className="creator"
                to={`/u/${encodeURIComponent(user.username)}`}
              >
                <Avatar user={user} />
                <strong>{user.username}</strong>
              </Link>
              {friendship && <FriendActions user={{ ...user, friendship }} />}
            </div>
          ))}
        </div>
        <LoadMore query={query} />
      </Status>
    </section>
  );
}

function Activity({ userId }) {
  const query = usePages(
    `/social/users/${userId}/watch-activity`,
    "activity",
    {},
    { pageSize: 30 },
  );
  const rows = pageItems(query, "activity", "clip_id");
  return (
    <section>
      <h2>История открытых клипов</h2>
      <p className="muted">
        Открытие клипа не означает просмотр видео до конца.
      </p>
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
      >
        {rows.length === 0 && (
          <p className="empty-state">История пока пуста.</p>
        )}
        <div className="activity-list">
          {rows.map((item) => (
            <Link key={item.clip_id} to={`/clip/${item.clip_id}`}>
              <PlayPreview src={item.thumbnail_url} />
              <span>
                {item.title || "Открыть клип"}
                <small>{date(item.last_viewed_at || item.viewed_at)}</small>
              </span>
              <ArrowUpRight size={16} />
            </Link>
          ))}
        </div>
        <LoadMore query={query} />
      </Status>
    </section>
  );
}

function PlayPreview({ src }) {
  return src ? (
    <img className="small-preview" src={src} alt="" loading="lazy" />
  ) : (
    <div className="small-preview" />
  );
}

function Settings() {
  const query = useApi("/user/settings");
  return (
    <section className="narrow">
      <h2>Настройки ленты</h2>
      <Status
        loading={query.isPending}
        error={query.error}
        onRetry={query.refetch}
      >
        {query.data && <SettingsForm data={query.data} />}
      </Status>
    </section>
  );
}

function SettingsForm({ data }) {
  const [keywords, setKeywords] = useState(
    (data.mute_keywords || []).join("\n"),
  );
  const { languageKey } = usePreferences();
  const save = useMutation({
    mutationFn: () =>
      request("/user/settings", {
        method: "PUT",
        body: {
          mute_keywords: keywords
            .split("\n")
            .map((value) => value.trim())
            .filter(Boolean),
          clip_languages: languageKey ? languageKey.split(",") : [],
        },
      }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["api", "/user/settings"] });
      queryClient.invalidateQueries({ queryKey: ["recommendations"] });
      queryClient.invalidateQueries({ queryKey: ["pages"] });
    },
  });
  return (
    <form
      className="surface form-stack"
      onSubmit={(event) => {
        event.preventDefault();
        save.mutate();
      }}
    >
      <label>
        Скрывать клипы со словами
        <textarea
          value={keywords}
          onChange={(event) => setKeywords(event.target.value)}
          maxLength={4000}
          placeholder="Каждое слово или фраза — с новой строки"
        />
      </label>
      <Button variant="primary" busy={save.isPending}>
        Сохранить настройки
      </Button>
      {save.isSuccess && (
        <p role="status" className="feedback">
          Настройки сохранены
        </p>
      )}
      {save.error && <Status error={save.error} />}
    </form>
  );
}
