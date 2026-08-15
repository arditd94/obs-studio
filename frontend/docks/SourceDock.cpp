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

#include "SourceDock.hpp"

#include <OBSApp.hpp>
#include <components/MediaControls.hpp>
#include <qt-wrappers.hpp>
#include <utility/display-helpers.hpp>
#include <widgets/OBSBasic.hpp>
#include <widgets/OBSQTDisplay.hpp>

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

SourceDock::SourceDock(OBSSource source, QWidget *parent) : OBSDock(parent)
{
	weakSource = OBSGetWeakRef(source);

	setObjectName(QString::fromUtf8(obs_source_get_uuid(source)) + "_SourceDock");
	setWindowTitle(QString::fromUtf8(obs_source_get_name(source)));
	setAllowedAreas(Qt::AllDockWidgetAreas);
	setMinimumSize(120, 120);

	preview = new OBSQTDisplay(this);
	preview->setMinimumSize(64, 36);
	preview->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));

	auto addDrawCallback = [this]() {
		obs_display_add_draw_callback(preview->GetDisplay(), SourceDock::DrawPreview, this);
	};

	connect(preview.data(), &OBSQTDisplay::DisplayCreated, this, addDrawCallback);

	propertiesButton = new QPushButton(QTStr("Properties"), this);
	interactButton = new QPushButton(QTStr("Interact"), this);

	connect(propertiesButton.data(), &QPushButton::clicked, this, &SourceDock::OpenProperties);
	connect(interactButton.data(), &QPushButton::clicked, this, &SourceDock::OpenInteract);

	QHBoxLayout *buttons = new QHBoxLayout();
	buttons->setContentsMargins(0, 0, 0, 0);
	buttons->addWidget(propertiesButton);
	buttons->addWidget(interactButton);
	buttons->addStretch();

	mediaControls = new MediaControls(this);
	mediaControls->SetSource(source);

	QVBoxLayout *layout = new QVBoxLayout();
	layout->setContentsMargins(4, 4, 4, 4);
	layout->addWidget(preview, 1);
	layout->addWidget(mediaControls);
	layout->addLayout(buttons);

	QWidget *content = new QWidget(this);
	content->setLayout(layout);
	setWidget(content);

	UpdateButtons(source);

	signal_handler_t *handler = obs_source_get_signal_handler(source);
	sigs.emplace_back(handler, "rename", SourceDock::SourceRenamed, this);
	sigs.emplace_back(handler, "remove", SourceDock::SourceRemoved, this);
	sigs.emplace_back(handler, "destroy", SourceDock::SourceRemoved, this);
}

SourceDock::~SourceDock()
{
	if (preview && preview->GetDisplay()) {
		obs_display_remove_draw_callback(preview->GetDisplay(), SourceDock::DrawPreview, this);
	}
}

OBSSource SourceDock::GetSource() const
{
	return OBSGetStrongRef(weakSource);
}

void SourceDock::UpdateButtons(obs_source_t *source)
{
	const uint32_t flags = source ? obs_source_get_output_flags(source) : 0;

	/* Interaction only makes sense for sources that accept input, and media
	 * transport only for those that expose a controllable duration. */
	interactButton->setVisible((flags & OBS_SOURCE_INTERACTION) != 0);
	mediaControls->setVisible((flags & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0);
	propertiesButton->setEnabled(source != nullptr);
}

void SourceDock::DrawPreview(void *data, uint32_t cx, uint32_t cy)
{
	SourceDock *window = static_cast<SourceDock *>(data);

	OBSSource source = window->GetSource();
	if (!source) {
		return;
	}

	const uint32_t sourceCX = std::max(obs_source_get_width(source), 1u);
	const uint32_t sourceCY = std::max(obs_source_get_height(source), 1u);

	int x;
	int y;
	float scale;

	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);

	const int newCX = int(scale * float(sourceCX));
	const int newCY = int(scale * float(sourceCY));

	gs_viewport_push();
	gs_projection_push();
	const bool previous = gs_set_linear_srgb(true);

	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(source);

	gs_set_linear_srgb(previous);
	gs_projection_pop();
	gs_viewport_pop();
}

void SourceDock::SourceRenamed(void *param, calldata_t *data)
{
	SourceDock *window = static_cast<SourceDock *>(param);
	const char *name = calldata_string(data, "new_name");

	if (name) {
		QMetaObject::invokeMethod(window, "HandleRename", Q_ARG(QString, QString::fromUtf8(name)));
	}
}

void SourceDock::SourceRemoved(void *param, calldata_t *)
{
	SourceDock *window = static_cast<SourceDock *>(param);

	/* The dock outlives the source only briefly; deleting it from the Qt
	 * thread keeps the draw callback teardown on the right thread. */
	QMetaObject::invokeMethod(window, "deleteLater");
}

void SourceDock::HandleRename(const QString &name)
{
	setWindowTitle(name);
}

void SourceDock::OpenProperties()
{
	OBSSource source = GetSource();

	if (source) {
		OBSBasic::Get()->OpenProperties(source);
	}
}

void SourceDock::OpenInteract()
{
	OBSSource source = GetSource();

	if (source) {
		OBSBasic::Get()->OpenInteraction(source);
	}
}
