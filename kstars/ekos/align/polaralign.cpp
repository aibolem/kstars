/*  PolarAlign class.
    Copyright (C) 2021 Hy Murveit

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.
 */

#include "polaralign.h"
#include "poleaxis.h"

#include <cmath>

#include "fitsviewer/fitsdata.h"
#include "kstarsdata.h"
#include "skypoint.h"
#include <ekos_align_debug.h>

/******************************************************************
PolarAlign is a class that supports polar alignment by determining the
mount's axis of rotation when given 3 solved images taken with RA mount
rotations between the images.

addPoint(image) is called by the polar alignment UI after it takes and
solves each of its three images. The solutions are store in SkyPoints (see below)
and are processed so that the sky positions correspond to "what's in the sky
now" and "at this geographic localtion".

Addpoint() samples the location of a particular pixel in its image.
When the 3 points are sampled, they should not be taken
from the center of the image, as HA rotations may not move that point
if the telescope and mount are well aligned. Thus, the points are sampled
from the edge of the image.

After all 3 images are sampled, findAxis() is called, which calls
PoleAxis::poleAxis() to solve for the mount's axis of rotation. FindAxis
then transforms poleAxis' result into azimuth and altitude offsets from the pole.

findCorrectedPixel() is given an x,y position on an image, and the offsets
generated by findAxis(), and computes a "corrected position" for that input
x,y point, such that if a user adjusted the GEM mount's altitude and azimuth
controls to move an object in the image's original x,y position to the corrected
position in the image,  the mount's axis of rotation should then coincide with the pole.
******************************************************************/


PolarAlign::PolarAlign(const GeoLocation *geo)
{
    if (geo == nullptr && KStarsData::Instance() != nullptr)
        geoLocation = KStarsData::Instance()->geo();
    else
        geoLocation = geo;
}

bool PolarAlign::northernHemisphere() const
{
    if ((geoLocation == nullptr) || (geoLocation->lat() == nullptr))
        return true;
    return geoLocation->lat()->Degrees() > 0;
}

void PolarAlign::reset()
{
    points.clear();
    times.clear();
}

// Gets the pixel's j2000 RA&DEC coordinates, converts to JNow,  adjust to
// the local time, and sets up the azimuth and altitude coordinates.
bool PolarAlign::prepareAzAlt(FITSData *image, const QPointF &pixel, SkyPoint *point) const
{
    // WCS must be set up for this image.
    SkyPoint coords;
    if (image->pixelToWCS(pixel, coords))
    {
        coords.apparentCoord(static_cast<long double>(J2000), image->getDateTime().djd());
        *point = SkyPoint::timeTransformed(&coords, image->getDateTime(), geoLocation, 0);
        return true;
    }
    return false;
}

bool PolarAlign::addPoint(FITSData *image)
{
    // Use the HA and DEC from the top-left of the image. Any point far from the center
    // of the image will do. The reason we don't use the center is as follows:
    // The goal is to measure 3 points with the mount rotated in 3 different anlges, and from
    // that deduce the mount's axis. If we use the center of the image, and the telescope is
    // very well aligned with the mount's axis, then all three measurements would be about
    // the same RA and DEC. This would make it difficult to estimate the mount's axis.
    // If, however, we offset the measurement from the center of the image,
    // then the measurements will differ in the 3 different rotations.
    SkyPoint coords;
    auto time = image->getDateTime();
    // 0,0 is the upper left corner of the image.
    if (!prepareAzAlt(image, QPointF(0, 0), &coords))
        return false;

    QString debugString = QString("PAA: addPoint ra0 %1 dec0 %2 ra %3 dec %4 az %5 alt %6")
                          .arg(coords.ra0().Degrees()).arg(coords.dec0().Degrees())
                          .arg(coords.ra().Degrees()).arg(coords.dec().Degrees())
                          .arg(coords.az().Degrees()).arg(coords.alt().Degrees());
    qCDebug(KSTARS_EKOS_ALIGN) << debugString;
    if (points.size() > 2)
        return false;
    points.push_back(coords);
    times.push_back(time);

    return true;
}

namespace
{
// Output is between -180 and +180 degrees.
double convertDegreesRange(double degrees)
{
    while (degrees < -180.0)
        degrees += 360.0;
    while (degrees > 180.0)
        degrees -= 360.0;
    return degrees;
}
}  // namespace

bool PolarAlign::findAxis()
{
    if (points.size() != 3)
        return false;

    SkyPoint sp;

    // poleAxis uses the .ra() and .dec() coordinates, which have already been converted
    // to JNow. The returned unit vector points towards the pole. We need to
    // convert it to an azimuth and altitiude.
    PoleAxis::V3 pa = PoleAxis::poleAxis(points[0], points[1], points[2]);

    if (pa.x() * pa.x() + pa.y() + pa.y() + pa.z() * pa.z() < 0.9)
    {
        // It failed to normalize the vector, something's wrong.
        qCDebug(KSTARS_EKOS_ALIGN) << "Normal vector too short. FindAxis failed.";
        return false;
    }

    // Need to make sure we're pointing to the right pole.
    if (northernHemisphere() && (pa.z() < 0))
        pa = PoleAxis::V3(-pa.x(), -pa.y(), -pa.z());

    if (!northernHemisphere() && pa.z() > 0)
    {
        pa = PoleAxis::V3(-pa.x(), -pa.y(), -pa.z());
        // need to add 12 hours to the RA
        dms ra = PoleAxis::primary(pa);
        ra.setD(ra.Degrees() + 180.0);
        sp.setRA(ra);
    }
    else
        // This else clause runs for everything except a southern "flip".
        sp.setRA(PoleAxis::primary(pa));

    sp.setDec(PoleAxis::secondary(pa));

    SkyPoint transformed = sp.timeTransformed(&sp, times[2], geoLocation, 0);

    // Want the results in the range of -180 to +180 degrees
    azimuthCenter = convertDegreesRange(transformed.az().Degrees());
    altitudeCenter = transformed.alt().Degrees();

    return true;
}

void PolarAlign::getAxis(double *azAxis, double *altAxis) const
{
    *azAxis = azimuthCenter;
    *altAxis = altitudeCenter;
}

// Find the pixel in image corresponding to the desired azimuth & altitude.
bool PolarAlign::findAzAlt(FITSData *image, double azimuth, double altitude, QPointF *pixel) const
{
    SkyPoint spt;
    spt.setAz(azimuth);
    spt.setAlt(altitude);
    dms LST = geoLocation->GSTtoLST(image->getDateTime().gst());
    spt.HorizontalToEquatorial(&LST, geoLocation->lat());
    SkyPoint j2000Coord = spt.catalogueCoord(image->getDateTime().djd());
    QPointF imagePoint;
    if (!image->wcsToPixel(j2000Coord, *pixel, imagePoint))
    {
        QString debugString =
            QString("PolarAlign: Couldn't get pixel from WCS for az %1 alt %2 with j2000 RA %3 DEC %4")
            .arg(azimuth).arg(altitude).arg(j2000Coord.ra0().toHMSString())
            .arg(j2000Coord.dec0().toDMSString());
        qCDebug(KSTARS_EKOS_ALIGN) << debugString;
        return false;
    }
    return true;
}

// Calculate the mount's azimuth and altitude error given the known geographic location
// and the azimuth center and altitude center computed in findAxis().
void PolarAlign::calculateAzAltError(double *azError, double *altError) const
{

    const double latitudeDegrees = geoLocation->lat()->Degrees();
    *altError = northernHemisphere() ?
                altitudeCenter - latitudeDegrees : altitudeCenter + latitudeDegrees;
    *azError = northernHemisphere() ? azimuthCenter : azimuthCenter + 180.0;
    while (*azError > 180.0)
        *azError -= 360;

    dms polarError(hypot(*altError, *azError));
    QString debugString = QString("PAA Error: %1 %2  up: %3 %4  az: %5 %6 latitude %7")
                          .arg(polarError.toDMSString()).arg(hypot(*altError, *azError), 6, 'f', 3)
                          .arg(dms(*altError).toDMSString()).arg(*altError, 6, 'f', 3)
                          .arg(dms(*azError).toDMSString()).arg(*azError, 6, 'f', 3)
                          .arg(latitudeDegrees, 6, 'f', 3);
    qCDebug(KSTARS_EKOS_ALIGN) << debugString;
}

// Given a pixel, find its RA/DEC, then its alt/az, and then search for another pixel
// with the right correction.
bool PolarAlign::findCorrectedPixel(FITSData *image, const QPointF &pixel, QPointF *corrected, bool altOnly)
{
    double azOffset, altOffset;
    calculateAzAltError(&azOffset, &altOffset);
    auto time = image->getDateTime();

    // 1. Find the az/alt for the x,y point on the image.
    SkyPoint p;
    if (!prepareAzAlt(image, pixel, &p))
        return false;
    double pixelAz = p.az().Degrees(), pixelAlt = p.alt().Degrees();

    QString debugString = QString("PAA: Pixel(%1,%2): Az: %3, Alt: %4")
                          .arg(pixel.x(), 4, 'f', 0).arg(pixel.y(), 4, 'f', 0)
                          .arg(pixelAz).arg(pixelAlt);
    qCDebug(KSTARS_EKOS_ALIGN) << debugString;

    // 2. Apply the az/alt offsets.
    // We know that the pole's az and alt offsets are effectively rotations
    // of a sphere. The offsets that apply to correct different points depend
    // on where (in the sphere) those points are. Points close to the pole can probably
    // just add the pole's offsets. This calculation is a bit more precise, and is
    // necessary if the points are not near the pole.
    QPointF rotated;
    if (altOnly)
        azOffset = 0.0;
    PolarAlign::rotate(QPointF(pixelAz, pixelAlt), QPointF(azOffset, altOffset), &rotated);
    const double targetAz = rotated.x(), targetAlt = rotated.y();

    debugString = QString("PAA: Target%1: Az: %2, Alt: %3")
                  .arg(altOnly ? "--Only Alt" : "").arg(targetAz, 6, 'f', 3).arg(targetAlt, 6, 'f', 3);
    qCDebug(KSTARS_EKOS_ALIGN) << debugString;

    // 3. Find a pixel with those alt/az values.
    if (!findAzAlt(image, targetAz, targetAlt, corrected))
        return false;

    return true;
}

double d2r(double degrees)
{
    return 2 * M_PI * degrees / 360.0;
}

void PolarAlign::rotate(const QPointF &azAltPoint, const QPointF &azAltRotation, QPointF *azAltResult) const
{
    const double alt = azAltPoint.y();

    // First rotate Azimuth
    const double az = azAltPoint.x() + azAltRotation.x();

    const double azRadians = d2r(az);
    const double altRadians = d2r(alt);

    // Convert the new point to xyz
    // See https://mathworld.wolfram.com/SphericalCoordinates.html
    // In this coordinate system, x points to the pole
    // y points to the left
    // z points up.
    // In this system, theta is the angle between x & y and is related
    // to our azimuth: theta = 90-azimuth.
    // Phi is the angle away from z, and is related to our altitude as 90-altitude.
    const double theta = -azRadians;
    const double phi = (M_PI / 2.0) - altRadians;
    const double x = cos(theta) * sin(phi);
    const double y = sin(theta) * sin (phi);
    const double z = cos(phi);

    // Multiply [x,y,z] by the rotate-Y by "angle" rotation matrix
    // as in https://en.wikipedia.org/wiki/Rotation_matrix
    //   cos(angle)  0   sin(angle)
    //       0       1       0
    //  -sin(angle)  0   cos(angle)

    const double angle = northernHemisphere() ?
                         d2r(-azAltRotation.y()) : d2r(azAltRotation.y());

    const double x2 =  x * cos(angle)     + z * sin(angle);
    const double y2 =                  y;
    const double z2 = -x * sin(angle)     + z * cos(angle);

    // Convert back to theta and phi
    double theta2;
    const double phi2 = acos(z2);

    // Deal with degenerate values for the atan along the meridian (y == 0).
    if (y == 0.0 && x == 0.0)
    {
        // Straight overhead
        azAltResult->setX(0.0);
        azAltResult->setY(90.0);
        return;
    }
    else if (y == 0)
        theta2 = 0.0;
    else
        theta2 = atan2(y2, x2);

    // Convert back to az alt
    const double az2Radians = -theta2;
    const double alt2Radians = (M_PI / 2.0) - phi2;

    azAltResult->setX(360 * az2Radians / (2 * M_PI));
    azAltResult->setY(360 * alt2Radians / (2 * M_PI));
}
