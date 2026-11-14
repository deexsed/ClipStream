import { useEffect } from "react";
import { readToken, request } from "./api/http";

let pending = new Set();
let owner;
let flushTimer;
const listeners = new Set();
let pageObserver;
function notifyVisibility() {
  for (const listener of listeners) listener();
}
function subscribeVisibility(listener) {
  if (!listeners.size) {
    document.addEventListener("visibilitychange", notifyVisibility);
    pageObserver = new MutationObserver(notifyVisibility);
    pageObserver.observe(document.body, {
      subtree: true,
      childList: true,
      attributes: true,
      attributeFilter: ["open", "data-recommendation-active"],
    });
  }
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
    if (!listeners.size) {
      pageObserver.disconnect();
      pageObserver = null;
      document.removeEventListener("visibilitychange", notifyVisibility);
    }
  };
}
function enqueue(token) {
  const current = readToken();
  if (owner !== current) {
    pending.clear();
    owner = current;
  }
  pending.add(token);
  if (flushTimer) return;
  flushTimer = setTimeout(() => {
    flushTimer = null;
    const ids = [...pending];
    pending.clear();
    if (owner !== readToken()) return;
    for (let i = 0; i < ids.length; i += 48)
      request("/clips/impressions", {
        method: "POST",
        body: { delivery_ids: ids.slice(i, i + 48) },
      }).catch(() => {});
  }, 200);
}

export function useRecommendationImpressions(ref, identity) {
  useEffect(() => {
    const root = ref.current;
    if (!root || typeof IntersectionObserver === "undefined") return;
    const timers = new Map(),
      visible = new Set(),
      sent = new Set();
    const eligible = (element) =>
      document.visibilityState !== "hidden" &&
      !document.querySelector("dialog[open]") &&
      !element.querySelector(".preroll, .preroll-loading") &&
      element.dataset.recommendationActive !== "false" &&
      element.isConnected;
    const cancel = (element) => {
      clearTimeout(timers.get(element));
      timers.delete(element);
    };
    const update = () => {
      for (const element of visible) {
        const token = element.dataset.recommendationDelivery;
        if (!eligible(element)) {
          cancel(element);
          continue;
        }
        if (sent.has(token) || timers.has(element)) continue;
        timers.set(
          element,
          setTimeout(() => {
            timers.delete(element);
            if (visible.has(element) && eligible(element)) {
              sent.add(token);
              enqueue(token);
            }
          }, 1000),
        );
      }
    };
    const observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (entry.isIntersecting && entry.intersectionRatio >= 0.5)
            visible.add(entry.target);
          else {
            visible.delete(entry.target);
            cancel(entry.target);
          }
        }
        update();
      },
      { threshold: [0, 0.5, 1] },
    );
    const elements = root.matches("[data-recommendation-delivery]")
      ? [root]
      : root.querySelectorAll("[data-recommendation-delivery]");
    for (const element of elements)
      if (element.dataset.recommendationDelivery) observer.observe(element);
    const unsubscribe = subscribeVisibility(update);
    return () => {
      observer.disconnect();
      unsubscribe();
      for (const timer of timers.values()) clearTimeout(timer);
    };
  }, [ref, identity]);
}
