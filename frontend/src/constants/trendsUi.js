import { useClipLangStore } from '../store/useClipLangStore';

/**
 * Периоды чарта. key — значение ?trend= в query при переходе с трендов.
 */
export const TREND_PERIODS = [
  {
    key: 'today',
    period: 'today',
    tabLabel: 'Сегодня',
    pageTitle: 'Сейчас в тренде',
    subtitle: 'Самый быстрый рост просмотров за последние сутки',
    badgeLine: 'В ТРЕНДЕ',
    pill: 'Сегодня',
  },
  {
    key: 'week',
    period: 'week',
    tabLabel: 'Неделя',
    pageTitle: 'Популярное за неделю',
    subtitle: 'Топ по просмотрам за 7 дней',
    badgeLine: 'недели',
    pill: 'Неделя',
  },
  {
    key: 'month',
    period: 'month',
    tabLabel: 'Месяц',
    pageTitle: 'Популярное за месяц',
    subtitle: 'Топ по просмотрам за 30 дней',
    badgeLine: 'месяца',
    pill: 'Месяц',
  },
  {
    key: 'all',
    period: 'all',
    tabLabel: 'Всё время',
    pageTitle: 'За всё время',
    subtitle: 'Самые просматриваемые клипы в базе',
    badgeLine: 'за всё время',
    pill: 'Всё время',
  },
];

export const TREND_SECTIONS = TREND_PERIODS;

/** Сколько позиций в чарте трендов */
export const TREND_TOP_LIMIT = 50;

export function getTrendPeriodMeta(key) {
  return TREND_PERIODS.find((p) => p.key === key) || null;
}

export function getTrendSectionMeta(key) {
  return getTrendPeriodMeta(key);
}

export function buildTrendClipSearch(periodKey, rank) {
  const sp = new URLSearchParams();
  sp.set('trend', periodKey);
  sp.set('rank', String(rank));
  const codes = useClipLangStore.getState().codes;
  if (codes.length) sp.set('langs', [...codes].sort().join(','));
  return sp.toString();
}
