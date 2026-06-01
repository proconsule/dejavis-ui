import React, { useEffect, useRef, useState } from 'react';
import { cn } from "@/lib/utils";

interface MarqueeProps {
  children: React.ReactNode;
  className?: string;
  speed?: 'slow' | 'fast';
  pauseOnHover?: boolean;
}

export function Marquee({ 
  children, 
  className, 
  speed = 'slow', 
  pauseOnHover = true 
}: MarqueeProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const contentRef = useRef<HTMLDivElement>(null);
  const [shouldAnimate, setShouldAnimate] = useState(false);

  useEffect(() => {
    if (!containerRef.current || !contentRef.current) return;

    const checkOverflow = () => {
      if (containerRef.current && contentRef.current) {
        // Confrontiamo la larghezza reale del testo con quella disponibile nel contenitore
        const hasOverflow = contentRef.current.scrollWidth > containerRef.current.clientWidth;
        setShouldAnimate(hasOverflow);
      }
    };

    // Il ResizeObserver monitora il contenitore (utile se la sidebar si restringe/allarga)
    const resizeObserver = new ResizeObserver(() => checkOverflow());
    resizeObserver.observe(containerRef.current);

    // Controlliamo anche al caricamento e se il contenuto cambia
    checkOverflow();

    return () => resizeObserver.disconnect();
  }, [children]); 

  return (
    <div 
      ref={containerRef} 
      className={cn("overflow-hidden whitespace-nowrap w-full relative", className)}
    >
      <div className={cn(
        "flex w-fit items-center",
        shouldAnimate && (speed === 'fast' ? "animate-marquee-fast" : "animate-marquee-slow"),
        shouldAnimate && pauseOnHover && "pause-on-hover"
      )}>
        {/* Blocco Principale */}
        <div ref={contentRef} className={cn("inline-block", shouldAnimate && "pr-12")}>
          {children}
        </div>
        
        {/* Blocco per il loop (appare solo se c'è overflow) */}
        {shouldAnimate && (
          <div className="inline-block pr-12">
            {children}
          </div>
        )}
      </div>
    </div>
  );
}