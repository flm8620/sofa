/******************************************************************************
*       SOFA, Simulation Open-Framework Architecture, version 1.0 beta 3      *
*                (c) 2006-2008 MGH, INRIA, USTL, UJF, CNRS                    *
*                                                                             *
* This library is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as published by    *
* the Free Software Foundation; either version 2.1 of the License, or (at     *
* your option) any later version.                                             *
*                                                                             *
* This library is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
* for more details.                                                           *
*                                                                             *
* You should have received a copy of the GNU Lesser General Public License    *
* along with this library; if not, write to the Free Software Foundation,     *
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.          *
*******************************************************************************
*                               SOFA :: Modules                               *
*                                                                             *
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#include <sofa/component/forcefield/SurfacePressureForceField.h>
#include <sofa/core/componentmodel/topology/BaseMeshTopology.h>
#include <sofa/helper/gl/template.h>
#include <vector>
#include <set>
#include <iostream>

namespace sofa
{

namespace component
{

namespace forcefield
{

using namespace sofa::defaulttype;
using namespace core::componentmodel::topology;


template <class DataTypes>
SurfacePressureForceField<DataTypes>::SurfacePressureForceField():
    pressure(initData(&pressure, (Real)0.0, "pressure", "Pressure force per unit area")),
    min(initData(&min, Coord(), "min", "Lower bond of the selection box")),
    max(initData(&max, Coord(), "max", "Lower bond of the selection box")),
    pulseMode(initData(&pulseMode, false, "pulseMode", "Cyclic pressure application")),
    pressureSpeed(initData(&pressureSpeed, (Real)0.0, "pressureSpeed", "Continuous pressure application in Pascal per second. Only active in pulse mode")),
    volumeConservationMode(initData(&volumeConservationMode, false, "volumeConservationMode", "Pressure variation follow the inverse of the volume variation")),
    defaultVolume(initData(&defaultVolume, (Real)-1.0, "defaultVolume", "Default Volume"))
{

}



template <class DataTypes>
SurfacePressureForceField<DataTypes>::~SurfacePressureForceField()
{

}



template <class DataTypes>
void SurfacePressureForceField<DataTypes>::init()
{
    this->core::componentmodel::behavior::ForceField<DataTypes>::init();
    _topology = getContext()->getMeshTopology();

    state = ( pressure.getValue() > 0 ) ? INCREASE : DECREASE;

    if (pulseMode.getValue() && (pressureSpeed.getValue() == 0.0))
    {
        std::cerr << "WARNING Default pressure speed value has been set in SurfacePressureForceField\n";
        pressureSpeed.setValue((Real)fabs( pressure.getValue()));
    }
}



template <class DataTypes>
void SurfacePressureForceField<DataTypes>::addForce(VecDeriv& f, const VecCoord& x, const VecDeriv& v)
{
    Real p = pulseMode.getValue() ? computePulseModePressure() : pressure.getValue();

    if (_topology)
    {
        if (volumeConservationMode.getValue())
        {
            if(defaultVolume == -1)
            {
                defaultVolume = computeMeshVolume(f,x);
            }
            else if(defaultVolume != 0)
            {
                p *= defaultVolume.getValue() / computeMeshVolume(f,x);
            }
        }

        if (_topology->getNbTriangles() > 0)
        {
            addTriangleSurfacePressure(f,x,v,p);
        }

        if (_topology->getNbQuads() > 0)
        {
            addQuadSurfacePressure(f,x,v,p);
        }
    }
}


template <class DataTypes>
typename SurfacePressureForceField<DataTypes>::Real SurfacePressureForceField<DataTypes>::computeMeshVolume(const VecDeriv& /*f*/, const VecCoord& x)
{
    typedef BaseMeshTopology::Triangle Triangle;
    typedef BaseMeshTopology::Quad Quad;

    Real volume = 0;
    int i = 0;

    for (i = 0; i < _topology->getNbTriangles(); i++)
    {
        Triangle t = _topology->getTriangle(i);
        Deriv ab = x[t[1]] - x[t[0]];
        Deriv ac = x[t[2]] - x[t[0]];
        volume += (ab.cross(ac))[2] * (x[t[0]][2] + x[t[1]][2] + x[t[2]][2]) / static_cast<Real>(6.0);
    }

    for (i = 0; i < _topology->getNbQuads(); i++)
    {
        Quad q = _topology->getQuad(i);

        Deriv ab = x[q[1]] - x[q[0]];
        Deriv ac = x[q[2]] - x[q[0]];
        Deriv ad = x[q[3]] - x[q[0]];

        volume += ab.cross(ac)[2] * (x[q[0]][2] + x[q[1]][2] + x[q[2]][2]) / static_cast<Real>(6.0);
        volume += ac.cross(ad)[2] * (x[q[0]][2] + x[q[2]][2] + x[q[3]][2]) / static_cast<Real>(6.0);
    }

    return volume;
}


template <class DataTypes>
void SurfacePressureForceField<DataTypes>::addTriangleSurfacePressure(VecDeriv& f, const VecCoord& x, const VecDeriv& /*v*/, const Real& pressure)
{
    typedef BaseMeshTopology::Triangle Triangle;

    for (int i = 0; i < _topology->getNbTriangles(); i++)
    {
        Triangle t = _topology->getTriangle(i);

        if ( isInPressuredBox(x[t[0]]) && isInPressuredBox(x[t[1]]) && isInPressuredBox(x[t[2]]) )
        {

            Deriv ab = x[t[1]] - x[t[0]];
            Deriv ac = x[t[2]] - x[t[0]];

            Deriv p = pressure * (ab.cross(ac)) / static_cast<Real>(6.0);

            f[t[0]] += p;
            f[t[1]] += p;
            f[t[2]] += p;
        }
    }
}



template <class DataTypes>
void SurfacePressureForceField<DataTypes>::addQuadSurfacePressure(VecDeriv& f, const VecCoord& x, const VecDeriv& /*v*/, const Real& pressure)
{
    typedef BaseMeshTopology::Quad Quad;

    for (int i = 0; i < _topology->getNbQuads(); i++)
    {
        Quad q = _topology->getQuad(i);

        if ( isInPressuredBox(x[q[0]]) && isInPressuredBox(x[q[1]]) && isInPressuredBox(x[q[2]]) && isInPressuredBox(x[q[3]]) )
        {
            Deriv ab = x[q[1]] - x[q[0]];
            Deriv ac = x[q[2]] - x[q[0]];
            Deriv ad = x[q[3]] - x[q[0]];

            Deriv p1 = pressure * (ab.cross(ac)) / static_cast<Real>(6.0);
            Deriv p2 = pressure * (ac.cross(ad)) / static_cast<Real>(6.0);

            Deriv p = p1 + p2;

            f[q[0]] += p;
            f[q[1]] += p1;
            f[q[2]] += p;
            f[q[3]] += p2;
        }

    }
}



template <class DataTypes>
bool SurfacePressureForceField<DataTypes>::isInPressuredBox(const Coord &x) const
{
    if ( (max == Coord()) && (min == Coord()) )
        return true;

    return ( (x[0] >= min.getValue()[0])
            && (x[0] <= max.getValue()[0])
            && (x[1] >= min.getValue()[1])
            && (x[1] <= max.getValue()[1])
            && (x[2] >= min.getValue()[2])
            && (x[2] <= max.getValue()[2]) );
}



template <class DataTypes>
double SurfacePressureForceField<DataTypes>::getPotentialEnergy(const VecCoord& /*x*/)
{
    cerr<<"TrianglePressureForceField::getPotentialEnergy-not-implemented !!!"<<endl;
    return 0;
}



template<class DataTypes>
const typename SurfacePressureForceField<DataTypes>::Real SurfacePressureForceField<DataTypes>::computePulseModePressure()
{
    static Real p = 0;
    double dt = this->getContext()->getDt();

    if (state == INCREASE)
    {
        Real pUpperBound = (pressure.getValue() > 0) ? pressure.getValue() : 0.0;

        p += pressureSpeed.getValue() * dt;
        if (p >= pUpperBound)
        {
            p = pUpperBound;
            state = DECREASE;
        }
        return p;
    }

    if (state == DECREASE)
    {
        Real pLowerBound = (pressure.getValue() > 0) ? 0.0 : pressure.getValue();

        p -= pressureSpeed.getValue() * dt;
        if (p<= pLowerBound)
        {
            p = pLowerBound;
            state = INCREASE;
        }
        return p;
    }

    return 0.0;
}



template<class DataTypes>
void SurfacePressureForceField<DataTypes>::draw()
{
    if (!getContext()->getShowForceFields()) return;
    if (!this->mstate) return;

    if (getContext()->getShowWireFrame())
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);


    glDisable(GL_LIGHTING);

    glColor4f(0,1,0,1);

    glBegin(GL_LINE_LOOP);
    glVertex3d(min.getValue()[0],min.getValue()[1],min.getValue()[2]);
    glVertex3d(max.getValue()[0],min.getValue()[1],min.getValue()[2]);
    glVertex3d(max.getValue()[0],min.getValue()[1],max.getValue()[2]);
    glVertex3d(min.getValue()[0],min.getValue()[1],max.getValue()[2]);
    glEnd();

    glBegin(GL_LINE_LOOP);
    glVertex3d(min.getValue()[0],max.getValue()[1],min.getValue()[2]);
    glVertex3d(max.getValue()[0],max.getValue()[1],min.getValue()[2]);
    glVertex3d(max.getValue()[0],max.getValue()[1],max.getValue()[2]);
    glVertex3d(min.getValue()[0],max.getValue()[1],max.getValue()[2]);
    glEnd();

    glBegin(GL_LINES);
    glVertex3d(min.getValue()[0],min.getValue()[1],min.getValue()[2]);
    glVertex3d(min.getValue()[0],max.getValue()[1],min.getValue()[2]);

    glVertex3d(max.getValue()[0],min.getValue()[1],min.getValue()[2]);
    glVertex3d(max.getValue()[0],max.getValue()[1],min.getValue()[2]);

    glVertex3d(max.getValue()[0],min.getValue()[1],max.getValue()[2]);
    glVertex3d(max.getValue()[0],max.getValue()[1],max.getValue()[2]);

    glVertex3d(min.getValue()[0],min.getValue()[1],max.getValue()[2]);
    glVertex3d(min.getValue()[0],max.getValue()[1],max.getValue()[2]);
    glEnd();


    if (getContext()->getShowWireFrame())
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

} // namespace forcefield

} // namespace component

} // namespace sofa
