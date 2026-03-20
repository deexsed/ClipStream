import { useEffect, useRef, useCallback } from 'react';

export default function useInfiniteScroll(callback, hasMore, loading) {
  const observer = useRef(null);

  const lastElementRef = useCallback(
    (node) => {
      if (loading) return;
      if (observer.current) observer.current.disconnect();

      observer.current = new IntersectionObserver(
        (entries) => {
          if (entries[0].isIntersecting && hasMore) {
            callback();
          }
        },
        { rootMargin: '380px', threshold: 0 }
      );

      if (node) observer.current.observe(node);
    },
    [callback, hasMore, loading]
  );

  useEffect(() => {
    return () => {
      if (observer.current) observer.current.disconnect();
    };
  }, []);

  return lastElementRef;
}
