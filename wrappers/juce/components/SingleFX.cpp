//
// Created by Paul Walker on 1/7/22.
//

#include "SingleFX.h"
#include "SC3Editor.h"
#include "SCXTLookAndFeel.h"
#include "widgets/ComboBox.h"
#include "widgets/ParamEditor.h"

#include "sst/cpputils.h"

namespace SC3
{
namespace Components
{
SingleFX::SingleFX(SC3Editor *ed, int i) : editor(ed), idx(i)
{
    typeSelector = std::make_unique<SC3::Widgets::ComboBox>();
    typeSelector->onChange = [this]() { typeSelectorChanged(); };
    addAndMakeVisible(*typeSelector);

    for (auto i = 0; i < n_filter_parameters; ++i)
    {
        auto q = std::make_unique<Widgets::FloatParamEditor>(Widgets::FloatParamEditor::HSLIDER,
                                                             editor->multi.filters[idx].p[i]);
        addChildComponent(*q);
        fParams[i] = std::move(q);
    }

    for (auto i = 0; i < n_filter_iparameters; ++i)
    {
        auto q = std::make_unique<juce::Label>("IP " + std::to_string(i));
        q->setText("IP " + std::to_string(i), juce::NotificationType::dontSendNotification);
        addChildComponent(*q);
        iParams[i] = std::move(q);
    }
}
SingleFX::~SingleFX() = default;

void SingleFX::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::darkkhaki);

    auto r = getLocalBounds().withHeight(20);

    SCXTLookAndFeel::fillWithGradientHeaderBand(g, r, juce::Colour(0xFF333300));
    g.setColour(juce::Colours::white);
    g.setFont(SCXTLookAndFeel::getMonoFontAt(9));
    g.drawText("Effect " + std::to_string(idx + 1), r, juce::Justification::centred);
}

void SingleFX::resized()
{
    auto r = getLocalBounds().withHeight(20).translated(0, 25).reduced(2, 0);
    typeSelector->setBounds(r);

    r = r.translated(0, 25).withHeight(20);
    for (const auto &p : fParams)
    {
        p->setBounds(r);
        r = r.translated(0, 21);
    }
    for (const auto &p : iParams)
    {
        p->setBounds(r);
        r = r.translated(0, 21);
    }
}

static constexpr int idOff = 1023;
void SingleFX::onProxyUpdate()
{
    typeSelector->clear(juce::dontSendNotification);
    for (const auto &[fidx, t] : sst::cpputils::enumerate(editor->filterTypeNames))
    {
        typeSelector->addItem(t, fidx + idOff);
    }
    typeSelector->setSelectedId(editor->multi.filters[idx].type.val + idOff,
                                juce::dontSendNotification);

    const auto &fx = editor->multi.filters[idx];
    for (const auto &[fidx, t] : sst::cpputils::enumerate(fx.p))
    {
        fParams[fidx]->setVisible(!t.hidden);
    }
    for (const auto &[fidx, t] : sst::cpputils::enumerate(fx.ip))
    {
        iParams[fidx]->setVisible(!t.hidden);
        iParams[fidx]->setText(t.label, juce::dontSendNotification);
    }
}

void SingleFX::typeSelectorChanged()
{
    auto cidx = typeSelector->getSelectedId();
    auto ftype = cidx - idOff;

    actiondata ad;
    ad.id = ip_multi_filter_type;
    ad.actiontype = vga_intval;
    ad.subid = idx;
    ad.data.i[0] = ftype;
    editor->sendActionToEngine(ad);
}
} // namespace Components
} // namespace SC3