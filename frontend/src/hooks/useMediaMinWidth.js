import { useEffect, useState } from 'react';

/** Соответствует breakpoint `xl` в Tailwind (1280px). */
export function useMediaMinXl() {
  const [matches, setMatches] = useState(() =>
    typeof window !== 'undefined' ? window.matchMedia('(min-width: 1280px)').matches : false
  );

  useEffect(() => {
    const mq = window.matchMedia('(min-width: 1280px)');
    const onChange = () => setMatches(mq.matches);
    mq.addEventListener('change', onChange);
    return () => mq.removeEventListener('change', onChange);
  }, []);

  return matches;
}
