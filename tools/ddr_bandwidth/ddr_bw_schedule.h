#ifndef DDR_BW_SCHEDULE_H
#define DDR_BW_SCHEDULE_H

#include <cmath>

// First grid deadline strictly after now. Call only with positive intervalSec.
inline double NextCollectionDeadline(double startSec, double intervalSec, double nowSec)
{
    const double index = std::floor((nowSec - startSec) / intervalSec) + 1.0;
    const double deadline = startSec + index * intervalSec;
    return deadline > nowSec ? deadline : deadline + intervalSec;
}

#endif
