// PAD Engine — adaptador do Cloud Seed Core (Ghost Note Audio, licença MIT).
// "Reverb algorithm: Cloud Seed by Ghost Note Audio".
//
// O Cloud Seed já traz pré-delay, reflexões (multitap), difusão inicial modulada e uma
// cauda de várias linhas com difusão e equalização internas. Aqui mapeamos os parâmetros
// do PAD para um programa de "sala grande e escura" pensado para pads.
// A biblioteca foi adaptada para não alocar memória ao mudar parâmetros (third_party/README.md).
// O objeto interno ocupa alguns MB (buffers fixos) e por isso é criado em prepare().
#pragma once

#include <memory>
#include "ReverbBase.h"

namespace Cloudseed { class ReverbController; }

namespace pad {

class CloudReverb : public ReverbBase {
public:
    CloudReverb();
    ~CloudReverb() override;
    void prepare(float sampleRate) override;
    void reset() override;
    void setParams(const ReverbParams& p) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;

private:
    void apply(int id, double v);
    std::unique_ptr<Cloudseed::ReverbController> rc_;
    ReverbParams p_;
    bool first_ = true;
};

} // namespace pad
