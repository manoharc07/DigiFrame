/* DigiFrame — live scores: the sport registry.

   The single place that binds the sport modules together. Adding a sport is a
   three-step job and this file is two of them:

     1. write sport_<name>.h exporting a `static const SportModule SPORT_<NAME>`
        (catalogue, event map, drawBody, optional drawEvent, simulate)
     2. #include it below
     3. add it to SPORTS_[]

   Nothing else in the firmware changes: the dashboard dropdown, the favourites
   store, the poll task, the event ring and the widget all walk this table. */
#pragma once

#include "sport_cricket.h"
#include "sport_football.h"
#include "sport_basketball.h"
#include "sport_nfl.h"
#include "sport_hockey.h"
#include "sport_rugby.h"

static const SportModule *const SPORTS_[] = {
  &SPORT_CRICKET,
  &SPORT_FOOTBALL,
  &SPORT_BASKETBALL,
  &SPORT_NFL,
  &SPORT_HOCKEY,
  &SPORT_RUGBY,
};

/* the extern pair sports_core.h declared */
const SportModule *const *const SPORTS = SPORTS_;
const uint8_t NUM_SPORTS = (uint8_t)(sizeof(SPORTS_) / sizeof(SPORTS_[0]));
