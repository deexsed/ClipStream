import { useState, useEffect, useCallback } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import ClipViewerPanel from '../components/clips/ClipViewerPanel';
import RecommendedClips from '../components/clips/RecommendedClips';

export default function ClipPage() {
  const { id } = useParams();
  const navigate = useNavigate();
  const [relatedQueue, setRelatedQueue] = useState([]);

  const handleRelatedQueue = useCallback((clips) => {
    setRelatedQueue(Array.isArray(clips) ? clips : []);
  }, []);

  useEffect(() => {
    setRelatedQueue([]);
  }, [id]);

  const onFetchError = useCallback(() => navigate('/'), [navigate]);
  const onPlayNextNavigate = useCallback((nextId) => navigate(`/clip/${nextId}`), [navigate]);
  const nextClipId = relatedQueue[0]?.id ?? null;

  return (
    <div className="max-w-[1760px] mx-auto px-4 lg:px-8 py-6 lg:py-10">
      <div className="grid grid-cols-1 xl:grid-cols-12 gap-8 xl:gap-10">
        <div className="xl:col-span-8">
          <ClipViewerPanel
            key={id}
            clipId={id}
            showBreadcrumbs
            showPlayNextToggle
            nextClipId={nextClipId}
            onPlayNextNavigate={onPlayNextNavigate}
            onFetchError={onFetchError}
            recordViewMode="onMount"
          />
        </div>

        <aside className="xl:col-span-4 xl:sticky xl:top-24 self-start space-y-5">
          <RecommendedClips currentClipId={Number(id)} onQueueChange={handleRelatedQueue} />
        </aside>
      </div>
    </div>
  );
}
