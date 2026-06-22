import * as React from "react"
import {useEffect, useState} from "react";

const MOBILE_BREAKPOINT = 768

export function useIsMobile() {
  const [isMobile, setIsMobile] = React.useState<boolean | undefined>(undefined)

  React.useEffect(() => {
    const mql = window.matchMedia(`(max-width: ${MOBILE_BREAKPOINT - 1}px)`)
    const onChange = () => {
      setIsMobile(window.innerWidth < MOBILE_BREAKPOINT)
    }
    mql.addEventListener("change", onChange)
    setIsMobile(window.innerWidth < MOBILE_BREAKPOINT)
    return () => mql.removeEventListener("change", onChange)
  }, [])

  return !!isMobile
}

export function useDeviceCapabilities() {
  const [capabilities, setCapabilities] = useState({
    isMobile: false,
    isTouch: false,
    isLowPower: false,
    viewport: 'lg' // sm, md, lg
  });

  useEffect(() => {
    const updateCaps = () => {
      const width = window.innerWidth;
      const ua = navigator.userAgent.toLowerCase();

      setCapabilities({
        isMobile: /android|iphone|ipad/.test(ua),
        isTouch: 'ontouchstart' in window || navigator.maxTouchPoints > 0,
        isLowPower: false,
        viewport: width < 640 ? 'sm' : width < 1024 ? 'md' : 'lg'
      });
    };

    updateCaps();
    window.addEventListener('resize', updateCaps);
    return () => window.removeEventListener('resize', updateCaps);
  }, []);

  return capabilities;
}
