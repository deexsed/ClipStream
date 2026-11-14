import { useEffect, useRef, useState } from "react";
import { ArrowUpRight } from "lucide-react";
import { useQuery } from "@tanstack/react-query";
import { request, queryString } from "../../shared/api/http";
import { Button } from "../../shared/ui/Primitives";
import { randomId } from "../../shared/randomId";

let browserSession;
function audience() {
  if (browserSession) return browserSession;
  try {
    browserSession = sessionStorage.getItem("clipstream.ads.session");
  } catch {}
  if (!browserSession) {
    browserSession = randomId();
    try {
      sessionStorage.setItem("clipstream.ads.session", browserSession);
    } catch {}
  }
  return browserSession;
}
function useAdvertisement(placement, identity) {
  const [instance] = useState(() => randomId());
  return useQuery({
    queryKey: ["advertisement", placement, identity, instance],
    queryFn: ({ signal }) =>
      request(
        `/ads/serve?${queryString({ placement, session_id: audience() })}`,
        { signal },
      ),
    staleTime: Infinity,
    gcTime: 60000,
    retry: false,
    refetchOnWindowFocus: false,
    refetchOnReconnect: false,
  });
}
const acknowledge = (data, event) =>
  request("/ads/events", {
    method: "POST",
    body: { serve_token: data.serve_token, event },
  });
function Disclosure({ ad }) {
  return (
    <small className="ad-disclosure">
      Реклама · {ad.advertiser}
      {ad.disclosure_token && <span> · erid: {ad.disclosure_token}</span>}
    </small>
  );
}
function AdLink({ data, children, className = "" }) {
  return (
    <a
      className={className}
      href={data.ad.click_url}
      target="_blank"
      rel="noopener noreferrer sponsored"
      onClick={() => {
        acknowledge(data, "click").catch(() => {});
      }}
    >
      {children}
    </a>
  );
}
export default function Advertisement({
  placement = "banner",
  identity = "global",
}) {
  const query = useAdvertisement(placement, identity);
  const ref = useRef(null);
  const [loadedProof, setLoadedProof] = useState(null);
  const [failed, setFailed] = useState(false);
  const proof = query.data?.serve_token;
  useEffect(() => {
    if (!proof || loadedProof !== proof || !ref.current) return;
    let timer;
    let recorded = false;
    const observer = new IntersectionObserver(
      ([entry]) => {
        clearTimeout(timer);
        if (entry.isIntersecting && entry.intersectionRatio >= 0.5 && !recorded)
          timer = setTimeout(() => {
            if (document.visibilityState === "visible") {
              recorded = true;
              acknowledge(query.data, "impression").catch(() => {});
              observer.disconnect();
            }
          }, 1000);
      },
      { threshold: [0.5] },
    );
    observer.observe(ref.current);
    return () => {
      clearTimeout(timer);
      observer.disconnect();
    };
  }, [proof, loadedProof]);
  if (!query.data?.ad || failed) return null;
  const { ad } = query.data;
  return (
    <aside
      className={`advertisement ad-${placement}`}
      aria-label="Реклама"
      ref={ref}
    >
      <AdLink data={query.data} className="ad-content">
        <img
          src={ad.image_url}
          alt={ad.title}
          loading="lazy"
          onLoad={() => setLoadedProof(proof)}
          onError={() => setFailed(true)}
        />
        <div>
          <Disclosure ad={ad} />
          <h2>{ad.title}</h2>
          {ad.body && <p>{ad.body}</p>}
          <span className="text-link">
            Подробнее <ArrowUpRight size={14} />
          </span>
        </div>
      </AdLink>
    </aside>
  );
}
export function Preroll({ clipId, children }) {
  const [started, setStarted] = useState(false);
  const [finished, setFinished] = useState(false);
  const [played, setPlayed] = useState(0);
  const [waitExpired, setWaitExpired] = useState(false);
  const ref = useRef(null);
  const query = useAdvertisement("preroll", clipId);
  const data = query.data;
  useEffect(() => {
    setFinished(false);
    setStarted(false);
    setPlayed(0);
    setWaitExpired(false);
    const timer = setTimeout(() => setWaitExpired(true), 1200);
    return () => clearTimeout(timer);
  }, [clipId]);
  useEffect(() => {
    if (waitExpired && query.isPending) setFinished(true);
  }, [waitExpired, query.isPending]);
  // Fetch failures and empty inventory must never hold the clip behind an advertisement.
  if (finished || query.isError || (waitExpired && query.isPending))
    return children;
  if (query.isPending)
    return (
      <div className="preroll-loading" role="status">
        Подготовка видео…
      </div>
    );
  if (!data?.ad) return children;
  const { ad } = data;
  return (
    <section className="preroll" aria-label="Реклама перед клипом">
      <div className="preroll-video">
        <video
          ref={ref}
          src={ad.video_url}
          poster={ad.image_url || undefined}
          controls
          playsInline
          preload="metadata"
          onPlaying={() => {
            if (!started) {
              setStarted(true);
              acknowledge(data, "impression").catch(() => {});
            }
          }}
          onTimeUpdate={(event) => setPlayed(event.currentTarget.currentTime)}
          onEnded={() => setFinished(true)}
          onError={() => setFinished(true)}
          onStalled={() => setFinished(true)}
        />
      </div>
      <div className="preroll-toolbar">
        <div>
          <Disclosure ad={ad} />
          <AdLink data={data} className="text-link">
            {ad.title} <ArrowUpRight size={14} />
          </AdLink>
        </div>
        <Button
          disabled={played < ad.skip_after_seconds}
          onClick={() => setFinished(true)}
        >
          {played < ad.skip_after_seconds
            ? `Пропустить через ${Math.ceil(ad.skip_after_seconds - played)} с`
            : "Перейти к клипу"}
        </Button>
      </div>
      {!started && (
        <button className="text-link" onClick={() => setFinished(true)}>
          Пропустить рекламу без воспроизведения
        </button>
      )}
    </section>
  );
}
