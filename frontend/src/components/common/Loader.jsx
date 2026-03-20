export default function Loader() {
  return (
    <div className="mt-12 flex justify-center pb-12">
      <div className="flex gap-2">
        <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" />
        <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" style={{ animationDelay: '0.1s' }} />
        <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" style={{ animationDelay: '0.2s' }} />
      </div>
    </div>
  );
}
