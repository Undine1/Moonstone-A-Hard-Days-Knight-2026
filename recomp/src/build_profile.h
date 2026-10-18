#ifndef MOON_BUILD_PROFILE_H
#define MOON_BUILD_PROFILE_H

/* Public is the fail-closed default, including builds outside our scripts.
 * Only the development recipe explicitly enables developer tools. */
#ifndef MOON_DEVELOPER
#define MOON_DEVELOPER 0
#endif
#if MOON_DEVELOPER != 0 && MOON_DEVELOPER != 1
#error MOON_DEVELOPER must be 0 or 1
#endif
#if MOON_DEVELOPER
#define MOON_BUILD_PROFILE "MOONSTONE_BUILD_PROFILE:development"
#else
#define MOON_BUILD_PROFILE "MOONSTONE_BUILD_PROFILE:release"
#endif

#endif
