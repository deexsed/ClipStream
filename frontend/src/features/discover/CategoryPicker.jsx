import {
  memo,
  useCallback,
  useDeferredValue,
  useId,
  useMemo,
  useRef,
  useState,
} from "react";
import { Check, Gamepad2, X } from "lucide-react";
import { Button, Dialog, Status } from "../../shared/ui/Primitives";
import { SelectButton } from "../../shared/ui/Select";
import SearchField from "../../shared/ui/SearchField";
import TwitchImage from "../../shared/ui/TwitchImage";
import { frequentCategories } from "../../shared/categoryHistory";
import { categoryLimit } from "./categoryFilter";
import VirtualCategoryGrid from "./VirtualCategoryGrid";
import "./CategoryPicker.css";

const CategoryCard = memo(function CategoryCard({
  game,
  selected,
  disabled,
  onToggle,
  index,
}) {
  return (
    <Button
      variant="plain"
      className="category-picker-card"
      aria-pressed={selected}
      data-category-index={index}
      disabled={disabled}
      onClick={() => onToggle(String(game.id))}
      title={game.name}
    >
      <span className="category-picker-art" aria-hidden="true">
        <Gamepad2 size={24} />
        {game.box_art && (
          <TwitchImage
            src={game.box_art}
            alt=""
            loading="lazy"
            onError={(event) => {
              event.currentTarget.hidden = true;
            }}
          />
        )}
        {selected && <Check className="category-picker-check" size={16} />}
      </span>
      <span className="category-picker-name">{game.name}</span>
    </Button>
  );
});

function CategoryGrid({ games, selected, onToggle }) {
  return (
    <div className="category-picker-grid">
      {games.map((game) => {
        const active = selected.has(String(game.id));
        return (
          <CategoryCard
            key={game.id}
            game={game}
            selected={active}
            disabled={!active && selected.size >= categoryLimit}
            onToggle={onToggle}
          />
        );
      })}
    </div>
  );
}

export default function CategoryPicker({
  value,
  onChange,
  games,
  viewed,
  viewer,
  loading,
  error,
  onRetry,
}) {
  const [open, setOpen] = useState(false);
  const trigger = useRef(null);
  const id = useId();
  const label =
    value.length > 1
      ? `Категории: ${value.length}`
      : value.length
        ? games.find((game) => String(game.id) === value[0])?.name ||
          "Выбранная категория"
        : "Все категории";

  return (
    <>
      <SelectButton
        ref={trigger}
        expanded={open}
        aria-label={`Категория клипов: ${label}`}
        aria-controls={open ? id : undefined}
        onClick={() => {
          trigger.current.focus({ preventScroll: true });
          setOpen(true);
        }}
      >
        {label}
      </SelectButton>
      <Dialog
        open={open}
        onClose={() => setOpen(false)}
        title="Категории клипов"
        className="category-picker-panel"
      >
        {open && (
          <CategorySelection
            id={id}
            value={value}
            games={games}
            viewed={viewed}
            viewer={viewer}
            loading={loading}
            error={error}
            onRetry={onRetry}
            onApply={(ids) => {
              onChange(ids);
              setOpen(false);
            }}
          />
        )}
      </Dialog>
    </>
  );
}

function CategorySelection({
  id,
  value,
  games,
  viewed,
  viewer,
  loading,
  error,
  onRetry,
  onApply,
}) {
  const [search, setSearch] = useState("");
  const [selected, setSelected] = useState(() => new Set(value));
  const scroll = useRef(null);
  const searchInput = useRef(null);
  const names = useMemo(
    () =>
      new Map(
        [...viewed, ...games].map((game) => [String(game.id), game.name]),
      ),
    [games, viewed],
  );
  const query = useDeferredValue(search.trim().toLocaleLowerCase());
  const index = useMemo(
    () =>
      games.map((game) => ({
        game,
        name: game.name.toLocaleLowerCase(),
      })),
    [games],
  );
  const frequent = useMemo(
    () => frequentCategories(games, viewed, viewer).slice(0, 4),
    [games, viewed, viewer],
  );
  const matches = useMemo(() => {
    if (query)
      return index
        .filter(({ name }) => name.includes(query))
        .map(({ game }) => game);
    const frequentIds = new Set(frequent.map((game) => String(game.id)));
    return games.filter((game) => !frequentIds.has(String(game.id)));
  }, [games, index, frequent, query]);
  const toggle = useCallback((gameId) => {
    setSelected((before) => {
      const next = new Set(before);
      if (next.has(gameId)) next.delete(gameId);
      else if (next.size < categoryLimit) next.add(gameId);
      return next;
    });
  }, []);
  const isDisabled = (game) =>
    selected.size >= categoryLimit && !selected.has(String(game.id));

  return (
    <div id={id} className="category-picker-content">
      <div className="category-picker-tools">
        <SearchField
          ref={searchInput}
          aria-label="Найти категорию"
          placeholder="Поиск по названию"
          value={search}
          onValueChange={(value) => {
            setSearch(value);
            if (scroll.current.scrollTop) scroll.current.scrollTop = 0;
          }}
        />
        {selected.size > 0 && (
          <ul
            className="category-picker-selected"
            aria-label="Выбранные категории"
          >
            {[...selected].map((gameId) => {
              const name = names.get(gameId) || `Категория №${gameId}`;
              return (
                <li key={gameId}>
                  <Button
                    variant="plain"
                    className="category-picker-chip"
                    aria-label={`Снять выбор: ${name}`}
                    title={name}
                    onClick={(event) => {
                      const item = event.currentTarget.parentElement;
                      const neighbor =
                        item.nextElementSibling || item.previousElementSibling;
                      (
                        neighbor?.firstElementChild || searchInput.current
                      )?.focus();
                      toggle(gameId);
                    }}
                  >
                    <span>{name}</span>
                    <X size={14} aria-hidden="true" />
                  </Button>
                </li>
              );
            })}
          </ul>
        )}
      </div>
      <div className="category-picker-scroll" ref={scroll}>
        <Status
          loading={loading && !games.length}
          error={games.length ? null : error}
          onRetry={onRetry}
        >
          {!query && frequent.length > 0 && (
            <section
              className="category-picker-frequent"
              aria-label="Часто выбираете и смотрите"
            >
              <h3>Часто выбираете и смотрите</h3>
              <CategoryGrid
                games={frequent}
                selected={selected}
                onToggle={toggle}
              />
            </section>
          )}
          {matches.length > 0 ? (
            <section
              aria-label={
                query ? "Результаты поиска категорий" : "Каталог категорий"
              }
            >
              <h3>{query ? `Найдено: ${matches.length}` : "Все категории"}</h3>
              <VirtualCategoryGrid
                games={matches}
                viewportSelector=".category-picker-scroll"
                className="category-picker-virtual"
                isDisabled={isDisabled}
                renderCard={(game, index) => (
                  <CategoryCard
                    key={game.id}
                    game={game}
                    index={index}
                    selected={selected.has(String(game.id))}
                    disabled={isDisabled(game)}
                    onToggle={toggle}
                  />
                )}
              />
            </section>
          ) : (
            (query || !games.length) && (
              <p className="category-picker-empty" role="status">
                {query
                  ? "Категории не найдены. Попробуйте другое название."
                  : "Категории пока не появились."}
              </p>
            )
          )}
        </Status>
      </div>
      <div className="category-picker-footer">
        <span className="muted" aria-live="polite">
          {selected.size
            ? `Выбрано: ${selected.size} из ${categoryLimit}`
            : "Все категории"}
        </span>
        <Button
          type="button"
          disabled={!selected.size}
          onClick={() => setSelected(new Set())}
        >
          Сбросить
        </Button>
        <Button
          type="button"
          variant="primary"
          onClick={() => onApply([...selected].sort())}
        >
          Применить
        </Button>
      </div>
    </div>
  );
}
