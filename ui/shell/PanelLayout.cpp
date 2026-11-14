#include "PanelLayout.h"

namespace sonicator
{
namespace
{
int clampTo(int value, int low, int high)
{
    return juce::jlimit(low, juce::jmax(low, high), value);
}
}

PanelLayoutPreferences defaultPanelLayout(int windowWidth)
{
    PanelLayoutPreferences layout;
    layout.foldersWidth = windowWidth < 1100 ? 190 : 240;
    layout.analysisWidth = windowWidth < 1100 ? 230 : 270;
    layout.previewHeight = minPreviewHeight;
    layout.favoritesHeight = 200;
    return layout;
}

PanelLayoutBounds computePanelLayout(juce::Rectangle<int> workspace, const PanelLayoutPreferences& preferred)
{
    PanelLayoutBounds out;
    if (workspace.getWidth() <= 0 || workspace.getHeight() <= 0) return out;

    const int width = workspace.getWidth();
    const int height = workspace.getHeight();
    auto defaults = defaultPanelLayout(width);

    int folders = preferred.foldersWidth > 0 ? preferred.foldersWidth : defaults.foldersWidth;
    int analysis = preferred.analysisWidth > 0 ? preferred.analysisWidth : defaults.analysisWidth;
    int preview = preferred.previewHeight > 0 ? preferred.previewHeight : defaults.previewHeight;
    folders = juce::jmax(1, folders);
    analysis = juce::jmax(1, analysis);

    // Height first: the preview keeps its preferred height unless the remaining centre area would
    // fall under its minimum.
    const int usableHeight = height;
    const int maxPreview = usableHeight - minLibraryHeight - panelDividerThickness;
    if (preview > maxPreview)
    {
        preview = juce::jmax(0, maxPreview);
        out.compact = true;
    }
    if (preview < minPreviewHeight && usableHeight - minLibraryHeight - panelDividerThickness >= minPreviewHeight)
        preview = minPreviewHeight;
    if (preview > usableHeight) preview = usableHeight;
    const int dividerHeight = usableHeight - preview > 0 ? juce::jmin(panelDividerThickness, usableHeight - preview) : 0;

    // Width: two dividers and three panels.
    int dividerWidth = panelDividerThickness;
    const int totalDividers = dividerWidth * 2;
    if (totalDividers + 3 > width)
    {
        dividerWidth = juce::jmax(0, (width - 3) / 2);
        out.compact = true;
    }
    const int available = juce::jmax(0, width - dividerWidth * 2);
    if (folders + analysis + minLibraryWidth > available)
    {
        // Shrink the two sides in proportion to how far each is above its own minimum. If even the
        // minimums do not fit, every panel takes a share of what is left.
        out.compact = true;
        const int sideBudget = available - juce::jmin(minLibraryWidth, juce::jmax(1, available / 3));
        if (folders + analysis > sideBudget && sideBudget > 0)
        {
            const int overflow = folders + analysis - sideBudget;
            const int foldersRoom = juce::jmax(0, folders - minFoldersWidth);
            const int analysisRoom = juce::jmax(0, analysis - minAnalysisWidth);
            const int room = foldersRoom + analysisRoom;
            if (room > 0)
            {
                const int fromFolders = (int) ((juce::int64) overflow * foldersRoom / room);
                folders -= juce::jmin(foldersRoom, fromFolders);
                analysis -= juce::jmin(analysisRoom, overflow - juce::jmin(foldersRoom, fromFolders));
            }
            const int stillOver = folders + analysis - sideBudget;
            if (stillOver > 0)
            {
                // Split what is left evenly, keeping at least one pixel for each side panel.
                const int share = sideBudget / 2;
                folders = juce::jmax(1, juce::jmin(folders, share));
                analysis = juce::jmax(1, juce::jmin(analysis, sideBudget - folders));
            }
        }
        else if (available < 3)
        {
            folders = 1;
            analysis = 1;
        }
    }
    folders = clampTo(folders, 1, juce::jmax(1, available - 2));
    analysis = clampTo(analysis, 1, juce::jmax(1, available - folders - 1));

    auto content = workspace;
    if (preview > 0)
    {
        out.preview = content.removeFromBottom(preview);
        if (dividerHeight > 0) out.previewDivider = content.removeFromBottom(dividerHeight);
    }
    else
    {
        out.preview = content.removeFromBottom(0);
    }
    out.folders = content.removeFromLeft(folders);
    if (dividerWidth > 0) out.foldersDivider = content.removeFromLeft(dividerWidth);
    out.analysis = content.removeFromRight(analysis);
    if (dividerWidth > 0) out.analysisDivider = content.removeFromRight(dividerWidth);
    out.library = content;
    return out;
}
} // namespace sonicator
