#ifndef cmFastbuildUtilityTargetGenerator_h
#define cmFastbuildUtilityTargetGenerator_h

#include "cmConfigure.h" // IWYU pragma: keep

#include <cmFastbuildTargetGenerator.h>

class cmFastbuildUtilityTargetGenerator : public cmFastbuildTargetGenerator
{
public:
  cmFastbuildUtilityTargetGenerator(cmGeneratorTarget* gt);

  virtual void Generate();
};

#endif // cmFastbuildUtilityTargetGenerator_h
