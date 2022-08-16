//
// Struct describing a single straw hit assigned to a track, for use in TTree diagnostics
//
#ifndef TrkStrawHitInfoHH
#define TrkStrawHitInfoHH
#include "Rtypes.h"
#include "Offline/DataProducts/inc/GenVector.hh"
namespace mu2e
{
  struct TrkStrawHitInfo {
    Int_t plane, panel, layer, straw;  // StrawId fields for the straw hit
    Int_t state;     // hit state, including activity and left-right ambiguity
    Int_t algo; // updater algorithm last used on this hit
    Bool_t frozen; // was state frozen?
    Int_t driftend; // which end(s) was used in computing the drift
    Float_t edep;        // reco energy deposition
    Float_t htime;   // raw hit time
    Float_t wdist;       // raw hit U position
    Float_t werr;    // raw hit U position error estimate
    Float_t totdrift; // TOT expressed as drift time
    Float_t ptoca, stoca;    // reference particle (sensor) time of closest approach (TOCA)
    Float_t wdoca, wdocavar;   // reference (biased) DOCA from the track to the wire, signed by the angular momentum WRT the wire and the measurement end (and variance)
    Float_t wdt, wtocavar;   // reference (biased) time difference (and variance) at POCA
    Float_t doca, docavar; // unbiaed DOCA (and variance)
    Float_t dt, tocavar;   // fit (unbiased) dt and variance
    Float_t upos; // POCA position along the straw WRT the straw middle
    Float_t rdrift,rerr,dvel,lang; // drift information
    Float_t utresid, utresidmvar, utresidpvar; // unbiased time residual and associated measurement and parameter variances
    Float_t udresid, udresidmvar, udresidpvar; // unbiased distance residual and associated measurement and parameter variances
    Float_t rtresid, rtresidmvar, rtresidpvar; // reference time residual and associated measurement and parameter variances
    Float_t rdresid, rdresidmvar, rdresidpvar; // reference distance residual and associated measurement and parameter variances
    // not sure if we still want these
    Float_t wdot;
    XYZVectorF poca;
    bool dhit, dactive;

    TrkStrawHitInfo() : plane(-1), panel(-1), layer(-1), straw(-1), state(-10),driftend(-1),
    edep(0), htime(0), wdist(0), werr(0),totdrift(0),
    ptoca(0), stoca(0),
    wdoca(0), wdocavar(0), wdt(0), wtocavar(0),
    doca(0.0), docavar(0), dt(0), tocavar(0),
    upos(0),rdrift(0), rerr(0), dvel(0), lang(0),
    utresid(0), utresidmvar(0), utresidpvar(0),
    udresid(0), udresidmvar(0), udresidpvar(0),
    rtresid(0), rtresidmvar(0), rtresidpvar(0),
    rdresid(0), rdresidmvar(0), rdresidpvar(0),
    wdot(0), dhit(false), dactive(false) {}
  };
}
#endif
