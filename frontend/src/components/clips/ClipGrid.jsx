import ClipCard from './ClipCard';

export default function ClipGrid({ clips, onClipClick, liveMap = {}, clipCardProps }) {
  return (
    <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 2xl:grid-cols-5 gap-4 sm:gap-5 lg:gap-6">
      {clips.map((clip, i) => {
        const extra =
          typeof clipCardProps === 'function' ? clipCardProps(clip, i) : clipCardProps ?? {};
        return (
          <ClipCard
            key={clip.id}
            clip={clip}
            index={i}
            onClick={onClipClick}
            isLive={!!liveMap[clip.broadcaster_name]}
            {...extra}
          />
        );
      })}
    </div>
  );
}
