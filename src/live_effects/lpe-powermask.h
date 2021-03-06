#ifndef INKSCAPE_LPE_POWERMASK_H
#define INKSCAPE_LPE_POWERMASK_H

/*
 * Inkscape::LPEPowerMask
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include "live_effects/effect.h"
#include "live_effects/parameter/text.h"
#include "live_effects/lpegroupbbox.h"

namespace Inkscape {
namespace LivePathEffect {

class LPEPowerMask : public Effect, GroupBBoxEffect {
public:
    LPEPowerMask(LivePathEffectObject *lpeobject);
    virtual ~LPEPowerMask();
    virtual void doBeforeEffect (SPLPEItem const* lpeitem);
    virtual void doEffect (SPCurve * curve);
    virtual void doOnRemove (SPLPEItem const* /*lpeitem*/);
    virtual Gtk::Widget * newWidget();
    virtual void doOnVisibilityToggled(SPLPEItem const* lpeitem);
    //virtual void transform_multiply(Geom::Affine const& postmul, bool set);
    void toggleMask();
    void setMask();
private:
    BoolParam invert;
    BoolParam wrap;
    BoolParam background;
    //BoolParam lock;
    TextParam background_style;
    Geom::Path mask_box;
    bool hide_mask;
};

} //namespace LivePathEffect
} //namespace Inkscape
#endif
