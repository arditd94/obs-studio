#pragma once

#include "OBSQTDisplay.hpp"

class Multiview;

enum class ProjectorType {
	Source,
	Scene,
	Preview,
	StudioProgram,
	Multiview,
};

class OBSProjector : public OBSQTDisplay {
	Q_OBJECT

private:
	OBSWeakSourceAutoRelease weakSource;
	std::vector<OBSSignal> sigs;

	static void OBSRenderMultiview(void *data, uint32_t cx, uint32_t cy);
	static void OBSRender(void *data, uint32_t cx, uint32_t cy);
	static void OBSSourceRenamed(void *data, calldata_t *params);
	static void OBSSourceDestroyed(void *data, calldata_t *params);

	void mousePressEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void closeEvent(QCloseEvent *event) override;

	bool isAlwaysOnTop;
	bool isAlwaysOnTopOverridden = false;
	int savedMonitor = -1;
	ProjectorType type = ProjectorType::Source;

	Multiview *multiview = nullptr;

	bool ready = false;

	void UpdateMultiview();
	void UpdateProjectorTitle(QString name);

	QRect prevGeometry;
	void SetMonitor(int monitor);

	/* Fade overlay state, used when a projector is opened or closed through
	 * the Project button so the external screen does not cut to black. */
	uint64_t fadeStart = 0;
	uint64_t fadeDuration = 0;
	bool fadeIn = true;

	/* Alpha of the black overlay for the current frame, 0 when not fading. */
	float FadeAlpha() const;

	static void DrawFadeOverlay(uint32_t cx, uint32_t cy, float alpha);

private slots:
	void EscapeTriggered();
	void OpenFullScreenProjector();
	void ResizeToContent();
	void OpenWindowedProjector();
	void AlwaysOnTopToggled(bool alwaysOnTop);
	void ScreenRemoved(QScreen *screen);
	void RenameProjector(QString oldName, QString newName);

public:
	OBSProjector(QWidget *widget, obs_source_t *source_, int monitor, ProjectorType type_);
	~OBSProjector();

	OBSSource GetSource();
	ProjectorType GetProjectorType();
	int GetMonitor();
	static void UpdateMultiviewProjectors();
	void SetHideCursor();

	/* Fades the projector contents from or to black. A fade-out does not
	 * close the projector; the caller decides when to delete it. */
	void StartFade(bool in, uint32_t durationMs);

	bool IsAlwaysOnTop() const;
	bool IsAlwaysOnTopOverridden() const;
	void SetIsAlwaysOnTop(bool isAlwaysOnTop, bool isOverridden);
};
