import { useDeferredValue, useMemo, useRef, useState } from "react";
import { Link } from "react-router-dom";
import { number } from "../../shared/format";
import { Status } from "../../shared/ui/Primitives";
import SearchField from "../../shared/ui/SearchField";
import CategoryArtwork from "./CategoryArtwork";
import VirtualCategoryGrid from "./VirtualCategoryGrid";

const emptyGames = [];

export default function CategoryCatalog({ query }) {
  const [search, setSearch] = useState("");
  const searchRef = useRef(null);
  const term = useDeferredValue(search.trim().toLocaleLowerCase());
  const games = query.data?.games || emptyGames;
  const indexed = useMemo(
    () => games.map((game) => ({ game, name: game.name.toLocaleLowerCase() })),
    [games],
  );
  const matches = useMemo(
    () =>
      term
        ? indexed
            .filter(({ name }) => name.includes(term))
            .map(({ game }) => game)
        : games,
    [games, indexed, term],
  );
  return (
    <section aria-label="Каталог категорий">
      <div className="category-catalog-search">
        <SearchField
          ref={searchRef}
          aria-label="Найти категорию"
          placeholder="Поиск по названию"
          value={search}
          onValueChange={(value) => {
            setSearch(value);
            const viewport = searchRef.current?.closest("#page-content");
            if (viewport) viewport.scrollTop = 0;
          }}
        />
      </div>
      <Status
        error={query.error}
        loading={query.isPending}
        onRetry={query.refetch}
        empty={!games.length && "Категории пока не появились"}
      >
        {matches.length ? (
          <VirtualCategoryGrid
            className="category-catalog-grid"
            viewportSelector="#page-content"
            games={matches}
            renderCard={(game, index) => (
              <Link
                className="category-card"
                key={game.id}
                to={`/category/${game.id}`}
                data-category-index={index}
              >
                <div>
                  <CategoryArtwork game={game} size={40} />
                </div>
                <h2 title={game.name}>{game.name}</h2>
                <p>{number(game.clip_count)} клипов</p>
              </Link>
            )}
          />
        ) : (
          <p className="muted" role="status">
            Категории не найдены. Попробуйте другое название.
          </p>
        )}
      </Status>
    </section>
  );
}
