/******************************************************************************
    Copyright (C) 2026 by the OBS Studio contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSBasicLayouts.hpp"
#include "NameDialog.hpp"

#include <OBSApp.hpp>
#include <components/FlowLayout.hpp>
#include <qt-wrappers.hpp>
#include <widgets/OBSBasic.hpp>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace {

struct SceneItemEntry {
	int64_t id;
	std::string name;
};

/* Collects the scene's items bottom to top so the combo order matches the
 * stacking the layout will produce. */
std::vector<SceneItemEntry> CollectSceneItems(obs_scene_t *scene)
{
	std::vector<SceneItemEntry> entries;

	if (!scene)
		return entries;

	auto enumItem = [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
		auto *list = static_cast<std::vector<SceneItemEntry> *>(param);
		obs_source_t *source = obs_sceneitem_get_source(item);
		const char *name = source ? obs_source_get_name(source) : nullptr;

		list->push_back({obs_sceneitem_get_id(item), name ? name : ""});

		return true;
	};

	obs_scene_enum_items(scene, enumItem, &entries);

	return entries;
}

} // namespace

OBSBasicLayouts::OBSBasicLayouts(OBSBasic *parent) : QDialog(parent), main(parent)
{
	setWindowTitle(QTStr("Basic.Layouts"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setMinimumSize(820, 520);

	/* A fixed default size overflows the bottom of the screen on laptops and
	 * on scaled displays, putting Apply out of reach, so the initial size is
	 * derived from the space actually available. */
	QScreen *screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
	const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);

	resize(std::min(1180, (int)(available.width() * 0.92)), std::min(880, (int)(available.height() * 0.88)));

	SceneLayoutManager::Instance().Load();

	BuildUI();
	RebuildPresets();

	const auto &layouts = SceneLayoutManager::Instance().Layouts();
	if (!layouts.empty())
		SelectLayout(layouts.front().id);
	else
		UpdateButtonStates();
}

void OBSBasicLayouts::BuildUI()
{
	/* --- slot assignment column ------------------------------------- */
	QGroupBox *slotGroup = new QGroupBox(QTStr("Basic.Layouts.Assignment"), this);
	slotBox = new QVBoxLayout();
	slotBox->setAlignment(Qt::AlignTop);

	QWidget *slotHost = new QWidget(this);
	slotHost->setLayout(slotBox);

	QScrollArea *slotScroll = new QScrollArea(this);
	slotScroll->setWidget(slotHost);
	slotScroll->setWidgetResizable(true);
	slotScroll->setFrameShape(QFrame::NoFrame);

	invertButton = new QPushButton(QTStr("Basic.Layouts.Invert"), this);
	invertButton->setToolTip(QTStr("Basic.Layouts.Invert.Tooltip"));
	connect(invertButton, &QPushButton::clicked, this, &OBSBasicLayouts::OnInvert);

	QVBoxLayout *slotGroupLayout = new QVBoxLayout();
	slotGroupLayout->addWidget(slotScroll, 1);
	slotGroupLayout->addWidget(invertButton);
	slotGroup->setLayout(slotGroupLayout);
	slotGroup->setMinimumWidth(260);
	slotGroup->setMaximumWidth(340);

	/* --- preset grid ------------------------------------------------- */
	QGroupBox *presetGroup = new QGroupBox(QTStr("Basic.Layouts.Presets"), this);

	presetContainer = new QWidget(this);
	presetFlow = new FlowLayout(presetContainer, 6, 6, 6);
	presetContainer->setLayout(presetFlow);

	QScrollArea *presetScroll = new QScrollArea(this);
	presetScroll->setWidget(presetContainer);
	presetScroll->setWidgetResizable(true);
	presetScroll->setFrameShape(QFrame::NoFrame);
	/* Without this the flow layout keeps its preferred width and the right
	 * hand thumbnails end up clipped instead of wrapping to the next row. */
	presetScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	QHBoxLayout *presetButtons = new QHBoxLayout();
	QPushButton *addButton = new QPushButton(QTStr("Basic.Layouts.Add"), this);
	duplicateButton = new QPushButton(QTStr("Basic.Layouts.Duplicate"), this);
	renameButton = new QPushButton(QTStr("Basic.Layouts.Rename"), this);
	deleteButton = new QPushButton(QTStr("Basic.Layouts.Delete"), this);
	QPushButton *importButton = new QPushButton(QTStr("Basic.Layouts.Import"), this);
	exportButton = new QPushButton(QTStr("Basic.Layouts.Export"), this);

	presetButtons->addWidget(addButton);
	presetButtons->addWidget(duplicateButton);
	presetButtons->addWidget(renameButton);
	presetButtons->addWidget(deleteButton);
	presetButtons->addStretch();
	presetButtons->addWidget(importButton);
	presetButtons->addWidget(exportButton);

	QVBoxLayout *presetGroupLayout = new QVBoxLayout();
	presetGroupLayout->addWidget(presetScroll, 1);
	presetGroupLayout->addLayout(presetButtons);
	presetGroup->setLayout(presetGroupLayout);

	/* --- editor ------------------------------------------------------ */
	QGroupBox *editorGroup = new QGroupBox(QTStr("Basic.Layouts.Editor"), this);

	editor = new LayoutView(this, LayoutView::Mode::Editor);

	addSlotButton = new QPushButton(QTStr("Basic.Layouts.AddSlot"), this);
	removeSlotButton = new QPushButton(QTStr("Basic.Layouts.RemoveSlot"), this);

	QGridLayout *alignGrid = new QGridLayout();
	alignGrid->setSpacing(3);

	/* Arrows keep the grid compact; the translated wording lives in the
	 * tooltip and the accessible name. */
	auto addAlignButton = [&](const char *glyph, const char *lookup, LayoutView::AlignAction action, int row,
				  int column) {
		QPushButton *button = new QPushButton(QString::fromUtf8(glyph), this);

		button->setToolTip(QTStr(lookup));
		button->setAccessibleName(QTStr(lookup));
		button->setMaximumWidth(44);

		connect(button, &QPushButton::clicked, this, [this, action]() { editor->AlignSelectedSlot(action); });

		alignGrid->addWidget(button, row, column);
		alignButtons.push_back(button);
	};

	addAlignButton("\xE2\x97\x80", "Basic.Layouts.Align.Left", LayoutView::AlignAction::Left, 0, 0);
	addAlignButton("\xE2\x86\x94", "Basic.Layouts.Align.HCenter", LayoutView::AlignAction::HCenter, 0, 1);
	addAlignButton("\xE2\x96\xB6", "Basic.Layouts.Align.Right", LayoutView::AlignAction::Right, 0, 2);
	addAlignButton("\xE2\x96\xB2", "Basic.Layouts.Align.Top", LayoutView::AlignAction::Top, 1, 0);
	addAlignButton("\xE2\x86\x95", "Basic.Layouts.Align.VCenter", LayoutView::AlignAction::VCenter, 1, 1);
	addAlignButton("\xE2\x96\xBC", "Basic.Layouts.Align.Bottom", LayoutView::AlignAction::Bottom, 1, 2);

	QPushButton *fillButton = new QPushButton(QTStr("Basic.Layouts.Align.Fill"), this);
	connect(fillButton, &QPushButton::clicked, this,
		[this]() { editor->AlignSelectedSlot(LayoutView::AlignAction::FillCanvas); });
	alignButtons.push_back(fillButton);

	QVBoxLayout *editorSide = new QVBoxLayout();
	editorSide->addWidget(addSlotButton);
	editorSide->addWidget(removeSlotButton);
	editorSide->addSpacing(10);
	editorSide->addWidget(new QLabel(QTStr("Basic.Layouts.Align"), this));
	editorSide->addLayout(alignGrid);
	editorSide->addWidget(fillButton);
	editorSide->addStretch();

	QHBoxLayout *editorLayout = new QHBoxLayout();
	editorLayout->addWidget(editor, 1);
	editorLayout->addLayout(editorSide);
	editorGroup->setLayout(editorLayout);

	/* --- assembly ---------------------------------------------------- */
	QHBoxLayout *topLayout = new QHBoxLayout();
	topLayout->addWidget(slotGroup);
	topLayout->addWidget(presetGroup, 1);

	statusLabel = new QLabel(QTStr("Basic.Layouts.EditorHint"), this);
	statusLabel->setWordWrap(true);

	applyButton = new QPushButton(QTStr("Basic.Layouts.Apply"), this);
	applyButton->setDefault(true);

	QPushButton *closeButton = new QPushButton(QTStr("Close"), this);

	QHBoxLayout *bottomLayout = new QHBoxLayout();
	bottomLayout->addWidget(statusLabel, 1);
	bottomLayout->addWidget(applyButton);
	bottomLayout->addWidget(closeButton);

	/* The preset grid is the primary surface, so it keeps the larger share
	 * of the vertical space and the editor stays secondary. */
	QVBoxLayout *mainLayout = new QVBoxLayout();
	mainLayout->addLayout(topLayout, 3);
	mainLayout->addWidget(editorGroup, 2);
	mainLayout->addLayout(bottomLayout, 0);
	setLayout(mainLayout);

	connect(addButton, &QPushButton::clicked, this, &OBSBasicLayouts::OnAdd);
	connect(duplicateButton, &QPushButton::clicked, this, &OBSBasicLayouts::OnDuplicate);
	connect(renameButton, &QPushButton::clicked, this, &OBSBasicLayouts::OnRename);
	connect(deleteButton, &QPushButton::clicked, this, &OBSBasicLayouts::OnDelete);
	connect(importButton, &QPushButton::clicked, this, &OBSBasicLayouts::OnImport);
	connect(exportButton, &QPushButton::clicked, this, &OBSBasicLayouts::OnExport);
	connect(addSlotButton, &QPushButton::clicked, this, &OBSBasicLayouts::OnAddSlot);
	connect(removeSlotButton, &QPushButton::clicked, this, &OBSBasicLayouts::OnRemoveSlot);
	connect(applyButton, &QPushButton::clicked, this, &OBSBasicLayouts::OnApply);
	connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
	connect(editor, &LayoutView::LayoutChanged, this, &OBSBasicLayouts::OnEditorChanged);
	connect(editor, &LayoutView::SlotSelected, this, &OBSBasicLayouts::OnSlotSelected);

	/* Match the preview to the canvas so the boxes are not misleading. */
	uint32_t cx = 0;
	uint32_t cy = 0;

	if (GetSceneCanvasSize(CurrentScene(), cx, cy) && cy > 0)
		editor->SetAspect((double)cx / (double)cy);
}

OBSScene OBSBasicLayouts::CurrentScene()
{
	return main ? main->GetCurrentScene() : OBSScene();
}

void OBSBasicLayouts::RebuildPresets()
{
	for (LayoutView *view : presetViews) {
		presetFlow->removeWidget(view);
		view->deleteLater();
	}

	presetViews.clear();

	double aspect = 16.0 / 9.0;
	uint32_t cx = 0;
	uint32_t cy = 0;

	if (GetSceneCanvasSize(CurrentScene(), cx, cy) && cy > 0)
		aspect = (double)cx / (double)cy;

	for (const SceneLayout &layout : SceneLayoutManager::Instance().Layouts()) {
		LayoutView *view = new LayoutView(presetContainer, LayoutView::Mode::Preview);

		view->SetLayout(layout);
		view->SetAspect(aspect);
		view->setFixedSize(150, 100);
		view->setToolTip(GetLayoutDisplayName(layout));
		view->setProperty("layoutId", QString::fromStdString(layout.id));
		view->SetSelected(layout.id == current.id);

		connect(view, &LayoutView::Clicked, this, &OBSBasicLayouts::OnPresetClicked);

		presetFlow->addWidget(view);
		presetViews.push_back(view);
	}
}

void OBSBasicLayouts::SelectLayout(const std::string &id)
{
	const SceneLayout *layout = SceneLayoutManager::Instance().Find(id);
	if (!layout)
		return;

	current = *layout;

	editor->SetLayout(current);
	editor->SetSelectedSlot(current.slotList.empty() ? -1 : 0);

	for (LayoutView *view : presetViews)
		view->SetSelected(view->property("layoutId").toString().toStdString() == id);

	RebuildSlotRows();
	UpdateButtonStates();
}

void OBSBasicLayouts::RebuildSlotRows()
{
	/* Remember the assignment so rebuilding after an edit does not reset
	 * what the user already picked. */
	std::vector<int64_t> previous;
	previous.reserve(slotCombos.size());

	for (QComboBox *combo : slotCombos)
		previous.push_back(combo->currentData().toLongLong());

	while (QLayoutItem *item = slotBox->takeAt(0)) {
		if (QWidget *widget = item->widget())
			widget->deleteLater();

		delete item;
	}

	slotCombos.clear();

	for (size_t i = 0; i < current.slotList.size(); i++) {
		QWidget *row = new QWidget(this);
		QHBoxLayout *rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);

		QLabel *label = new QLabel(QTStr("Basic.Layouts.Slot").arg(i + 1), row);
		label->setMinimumWidth(56);

		QComboBox *combo = new QComboBox(row);
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

		PopulateSourceCombo(combo, (int)i);

		if (i < previous.size()) {
			const int index = combo->findData(QVariant::fromValue(previous[i]));

			if (index >= 0)
				combo->setCurrentIndex(index);
		}

		connect(combo, &QComboBox::currentTextChanged, this, [this]() { UpdateSlotLabels(); });

		rowLayout->addWidget(label);
		rowLayout->addWidget(combo, 1);

		slotBox->addWidget(row);
		slotCombos.push_back(combo);
	}

	UpdateSlotLabels();
}

void OBSBasicLayouts::UpdateSlotLabels()
{
	QStringList labels;

	for (QComboBox *combo : slotCombos) {
		/* A slot left on "None" gets no caption rather than the word. */
		labels.append(combo->currentData().toLongLong() == 0 ? QString() : combo->currentText());
	}

	editor->SetSlotLabels(labels);
}

void OBSBasicLayouts::PopulateSourceCombo(QComboBox *combo, int slotIndex)
{
	combo->clear();
	combo->addItem(QTStr("Basic.Layouts.NoSource"), QVariant::fromValue((int64_t)0));

	const std::vector<SceneItemEntry> entries = CollectSceneItems(CurrentScene());

	for (const SceneItemEntry &entry : entries)
		combo->addItem(QString::fromStdString(entry.name), QVariant::fromValue(entry.id));

	/* Pre-assign the scene's own items in order, so opening the dialog on a
	 * scene that already holds the cameras needs no manual pairing. */
	if (slotIndex >= 0 && slotIndex < (int)entries.size())
		combo->setCurrentIndex(slotIndex + 1);
}

void OBSBasicLayouts::UpdateButtonStates()
{
	const bool hasLayout = !current.id.empty();
	const bool editable = hasLayout && !current.builtin;
	const bool hasScene = CurrentScene() != nullptr;

	duplicateButton->setEnabled(hasLayout);
	exportButton->setEnabled(hasLayout);
	renameButton->setEnabled(editable);
	deleteButton->setEnabled(editable);
	addSlotButton->setEnabled(editable && current.slotList.size() < kMaxLayoutSlots);
	removeSlotButton->setEnabled(editable && current.slotList.size() > 1);

	/* Alignment acts on the selected slot, so it needs both an editable
	 * layout and an actual selection. */
	const bool canAlign = editable && editor->SelectedSlot() >= 0;

	for (QPushButton *button : alignButtons)
		button->setEnabled(canAlign);
	applyButton->setEnabled(hasLayout && hasScene);

	/* Inverting acts on the assignments, not the layout, so it stays
	 * available on read-only built-in layouts too. */
	invertButton->setEnabled(slotCombos.size() >= 2);

	if (!hasScene)
		statusLabel->setText(QTStr("Basic.Layouts.NoScene"));
	else if (hasLayout && current.builtin)
		statusLabel->setText(QTStr("Basic.Layouts.BuiltinReadOnly"));
	else
		statusLabel->setText(QTStr("Basic.Layouts.EditorHint"));
}

void OBSBasicLayouts::CommitCurrentLayout()
{
	if (current.builtin || current.id.empty())
		return;

	SceneLayoutManager::Instance().AddOrReplace(current);
	SceneLayoutManager::Instance().Save();

	for (LayoutView *view : presetViews) {
		if (view->property("layoutId").toString().toStdString() == current.id)
			view->SetLayout(current);
	}
}

void OBSBasicLayouts::OnPresetClicked()
{
	LayoutView *view = qobject_cast<LayoutView *>(sender());
	if (!view)
		return;

	SelectLayout(view->property("layoutId").toString().toStdString());
}

void OBSBasicLayouts::OnEditorChanged()
{
	if (current.builtin) {
		/* Built-ins are read-only; put the untouched geometry back so the
		 * editor cannot drift out of sync with what will be applied. */
		editor->SetLayout(current);
		return;
	}

	current = editor->Layout();
	CommitCurrentLayout();
}

void OBSBasicLayouts::OnSlotSelected(int index)
{
	UNUSED_PARAMETER(index);
	UpdateButtonStates();
}

void OBSBasicLayouts::OnInvert()
{
	/* Reverses the assignment order rather than the layout geometry: the
	 * boxes stay where they are and the sources trade places, which is what
	 * "the cameras came out the wrong way round" calls for. With two slots
	 * this is simply a swap. */
	const size_t count = slotCombos.size();

	if (count < 2)
		return;

	for (size_t i = 0; i < count / 2; i++) {
		const int first = slotCombos[i]->currentIndex();
		const int second = slotCombos[count - 1 - i]->currentIndex();

		slotCombos[i]->setCurrentIndex(second);
		slotCombos[count - 1 - i]->setCurrentIndex(first);
	}

	UpdateSlotLabels();
}

void OBSBasicLayouts::OnAdd()
{
	std::string name;

	if (!NameDialog::AskForName(this, QTStr("Basic.Layouts.NewLayout.Title"),
				    QTStr("Basic.Layouts.NewLayout.Text"), name))
		return;

	if (name.empty())
		return;

	SceneLayout layout;
	layout.id = SceneLayoutManager::Instance().GenerateId(name);
	layout.name = name;
	layout.slotList = {{0.0f, 0.25f, 0.5f, 0.5f}, {0.5f, 0.25f, 0.5f, 0.5f}};

	if (!SceneLayoutManager::Instance().AddOrReplace(layout))
		return;

	SceneLayoutManager::Instance().Save();

	current = layout;
	RebuildPresets();
	SelectLayout(layout.id);
}

void OBSBasicLayouts::OnDuplicate()
{
	if (current.id.empty())
		return;

	std::string name = current.name + " (2)";

	if (!NameDialog::AskForName(this, QTStr("Basic.Layouts.NewLayout.Title"),
				    QTStr("Basic.Layouts.NewLayout.Text"), name))
		return;

	if (name.empty())
		return;

	SceneLayout layout = current;
	layout.id = SceneLayoutManager::Instance().GenerateId(name);
	layout.name = name;
	layout.builtin = false;

	if (!SceneLayoutManager::Instance().AddOrReplace(layout))
		return;

	SceneLayoutManager::Instance().Save();

	current = layout;
	RebuildPresets();
	SelectLayout(layout.id);
}

void OBSBasicLayouts::OnRename()
{
	if (current.id.empty() || current.builtin)
		return;

	std::string name = current.name;

	if (!NameDialog::AskForName(this, QTStr("Basic.Layouts.RenameLayout.Title"),
				    QTStr("Basic.Layouts.RenameLayout.Text"), name))
		return;

	if (name.empty())
		return;

	current.name = name;
	CommitCurrentLayout();
	RebuildPresets();
	SelectLayout(current.id);
}

void OBSBasicLayouts::OnDelete()
{
	if (current.id.empty() || current.builtin)
		return;

	const QString name = GetLayoutDisplayName(current);
	const auto button = OBSMessageBox::question(this, QTStr("Basic.Layouts.DeleteLayout.Title"),
						    QTStr("Basic.Layouts.DeleteLayout.Text").arg(name));

	if (button != QMessageBox::Yes)
		return;

	SceneLayoutManager::Instance().Remove(current.id);
	SceneLayoutManager::Instance().Save();

	current = SceneLayout();
	RebuildPresets();

	const auto &layouts = SceneLayoutManager::Instance().Layouts();
	if (!layouts.empty())
		SelectLayout(layouts.front().id);
	else
		UpdateButtonStates();
}

void OBSBasicLayouts::OnImport()
{
	const QString path = QFileDialog::getOpenFileName(this, QTStr("Basic.Layouts.Import"), QString(),
							  QTStr("Basic.Layouts.FileFilter"));

	if (path.isEmpty())
		return;

	QString error;

	if (!SceneLayoutManager::Instance().ImportFile(path, error)) {
		OBSMessageBox::warning(this, QTStr("Basic.Layouts.Import"), error);
		return;
	}

	RebuildPresets();

	const auto &layouts = SceneLayoutManager::Instance().Layouts();
	if (!layouts.empty())
		SelectLayout(layouts.back().id);
}

void OBSBasicLayouts::OnExport()
{
	if (current.id.empty())
		return;

	QString path = QFileDialog::getSaveFileName(this, QTStr("Basic.Layouts.Export"),
						    GetLayoutDisplayName(current) + ".json",
						    QTStr("Basic.Layouts.FileFilter"));

	if (path.isEmpty())
		return;

	QString error;

	if (!SceneLayoutManager::Instance().ExportFile(path, current, error))
		OBSMessageBox::warning(this, QTStr("Basic.Layouts.Export"), error);
}

void OBSBasicLayouts::OnAddSlot()
{
	if (current.builtin || current.slotList.size() >= kMaxLayoutSlots)
		return;

	/* New slots land slightly offset from the previous one so they are
	 * immediately visible and grabbable instead of hiding underneath. */
	LayoutSlot slot;

	if (!current.slotList.empty()) {
		slot = current.slotList.back();
		slot.x += 0.05f;
		slot.y += 0.05f;
	} else {
		slot = {0.25f, 0.25f, 0.5f, 0.5f};
	}

	slot.Normalize();
	current.slotList.push_back(slot);

	editor->SetLayout(current);
	editor->SetSelectedSlot((int)current.slotList.size() - 1);

	CommitCurrentLayout();
	RebuildSlotRows();
	UpdateButtonStates();
}

void OBSBasicLayouts::OnRemoveSlot()
{
	if (current.builtin || current.slotList.size() <= 1)
		return;

	int index = editor->SelectedSlot();

	if (index < 0 || index >= (int)current.slotList.size())
		index = (int)current.slotList.size() - 1;

	current.slotList.erase(current.slotList.begin() + index);

	editor->SetLayout(current);
	editor->SetSelectedSlot(std::min(index, (int)current.slotList.size() - 1));

	CommitCurrentLayout();
	RebuildSlotRows();
	UpdateButtonStates();
}

void OBSBasicLayouts::OnApply()
{
	OBSScene scene = CurrentScene();

	if (!scene || current.slotList.empty())
		return;

	uint32_t cx = 0;
	uint32_t cy = 0;

	if (!GetSceneCanvasSize(scene, cx, cy))
		return;

	const std::string undoData = SaveSceneTransforms(scene);

	/* Slot 1 is the background and each following slot stacks above it, so
	 * the order position is simply the slot index. */
	int order = 0;

	for (size_t i = 0; i < current.slotList.size() && i < slotCombos.size(); i++) {
		const int64_t id = slotCombos[i]->currentData().toLongLong();

		if (id == 0)
			continue;

		obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, id);

		if (!item)
			continue;

		if (ApplyLayoutSlot(item, current.slotList[i], cx, cy))
			obs_sceneitem_set_order_position(item, order++);
	}

	const std::string redoData = SaveSceneTransforms(scene);

	auto undoRedo = [](const std::string &data) {
		RestoreSceneTransforms(data);
	};

	main->undo_s.add_action(QTStr("Undo.Layout.Apply").arg(GetLayoutDisplayName(current)), undoRedo, undoRedo,
				undoData, redoData);
}
