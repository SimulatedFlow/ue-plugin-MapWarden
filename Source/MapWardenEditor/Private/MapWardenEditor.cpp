// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "MapWardenEditor.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Actor.h"
#include "MapWardenFocus.h"
#include "MapWardenLog.h"
#include "MapWardenScanner.h"
#include "MapWardenStatics.h"
#include "ToolMenus.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FMapWardenEditorModule"

namespace MapWardenEditorPrivate
{
	/** The world the editor is showing right now, which is the level the menu entries are about. */
	static UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	static void Toast(const FString& Message, const bool bSuccess)
	{
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = 8.0f;
		Info.bFireAndForget = true;

		const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

void FMapWardenEditorModule::StartupModule()
{
	// The runtime module's one question, answered. It produces findings that carry an actor path and has no
	// way to act on one, because selecting something in a viewport is an editor idea.
	FMapWardenFocus::Get().BindStatic(&FMapWardenEditorModule::FocusActor);

	// Deferred rather than done here. Tool menus are not necessarily up at module startup, and
	// RegisterStartupCallback is the engine's own answer to that: it runs the callback immediately if menus
	// are already available and queues it if they are not.
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMapWardenEditorModule::RegisterMenus));

	UE_LOG(LogMapWarden, Log, TEXT("MapWardenEditor started; Tools > MapWarden checks the open level."));
}

void FMapWardenEditorModule::ShutdownModule()
{
	FMapWardenFocus::Get().Unbind();

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FMapWardenEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (ToolsMenu == nullptr)
	{
		return;
	}

	FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("MapWarden"), LOCTEXT("MapWardenSection", "MapWarden"));

	Section.AddMenuEntry(
		TEXT("MapWardenScan"),
		LOCTEXT("MapWardenScanLabel", "Check This Level"),
		LOCTEXT("MapWardenScanTooltip",
			"Check every actor in the loaded level: duplicates on top of each other, collision switched off on meshes that have it, Movable things nothing moves, actors outside the level, triggers nobody listens to and references that no longer resolve. The full report goes to the Output Log."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMapWardenEditorModule::RunScanFromMenu)));

	Section.AddMenuEntry(
		TEXT("MapWardenSelect"),
		LOCTEXT("MapWardenSelectLabel", "Select Everything With A Finding"),
		LOCTEXT("MapWardenSelectTooltip",
			"Check the level and select every actor a finding is about, so you can see the whole list at once in the viewport and the outliner."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMapWardenEditorModule::SelectFindingsFromMenu)));

	Section.AddMenuEntry(
		TEXT("MapWardenReport"),
		LOCTEXT("MapWardenReportLabel", "Write Level Report"),
		LOCTEXT("MapWardenReportTooltip",
			"Check, then write the JSON report to the path in Project Settings. The same file MapWarden.Gate writes on a build server."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMapWardenEditorModule::RunReportFromMenu)));
}

void FMapWardenEditorModule::RunScanFromMenu()
{
	using namespace MapWardenEditorPrivate;

	UWorld* World = EditorWorld();
	if (World == nullptr)
	{
		Toast(TEXT("MapWarden: no level is open."), /*bSuccess=*/false);
		return;
	}

	const FMapWardenReport Report = FMapWardenScanner::RunWithProjectSettings(World);
	FMapWardenScanner::LogReport(Report, /*bAllFindings=*/true);

	Toast(FString::Printf(TEXT("%s\n%s\nThe full report is in the Output Log."),
		*UMapWardenStatics::FormatHeadline(Report),
		*UMapWardenStatics::FormatCoverage(Report)),
		Report.Verdict != EMapVerdict::Fail);
}

void FMapWardenEditorModule::RunReportFromMenu()
{
	using namespace MapWardenEditorPrivate;

	UWorld* World = EditorWorld();
	if (World == nullptr)
	{
		Toast(TEXT("MapWarden: no level is open."), /*bSuccess=*/false);
		return;
	}

	const FMapWardenReport Report = FMapWardenScanner::RunWithProjectSettings(World);
	FMapWardenScanner::LogReport(Report, /*bAllFindings=*/true);

	FString FullPath;
	const bool bWritten = FMapWardenScanner::WriteReportFile(Report, FString(), FullPath);

	Toast(bWritten
		? FString::Printf(TEXT("MapWarden report written:\n%s"), *FullPath)
		: FString::Printf(TEXT("MapWarden could not write the report to\n%s"), *FullPath),
		bWritten);
}

void FMapWardenEditorModule::SelectFindingsFromMenu()
{
	using namespace MapWardenEditorPrivate;

	UWorld* World = EditorWorld();
	if (World == nullptr || GEditor == nullptr)
	{
		Toast(TEXT("MapWarden: no level is open."), /*bSuccess=*/false);
		return;
	}

	const FMapWardenReport Report = FMapWardenScanner::RunWithProjectSettings(World);
	FMapWardenScanner::LogReport(Report, /*bAllFindings=*/true);

	GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);

	int32 Selected = 0;
	for (const FMapWardenFinding& Finding : Report.Findings)
	{
		// Excluded findings are not selected. They are still on the report and still counted - see
		// FMapWardenFinding::bExcluded - but somebody who asked to be shown the problems did not ask to be
		// shown the things they already decided were fine.
		if (Finding.bExcluded || Finding.ActorPath.IsEmpty())
		{
			continue;
		}

		if (AActor* Actor = FindObject<AActor>(nullptr, *Finding.ActorPath))
		{
			GEditor->SelectActor(Actor, /*bInSelected=*/true, /*bNotify=*/false);
			++Selected;
		}
	}

	GEditor->NoteSelectionChange();

	Toast(FString::Printf(TEXT("%s\n%d actor(s) selected."), *UMapWardenStatics::FormatHeadline(Report), Selected),
		Report.Verdict != EMapVerdict::Fail);
}

bool FMapWardenEditorModule::FocusActor(const FString& ActorPath)
{
	if (GEditor == nullptr || ActorPath.IsEmpty())
	{
		return false;
	}

	AActor* Actor = FindObject<AActor>(nullptr, *ActorPath);
	if (!IsValid(Actor))
	{
		// The level may have been reloaded, or the actor deleted since the check ran - which is a perfectly
		// ordinary thing to have happened, and a reason to return false rather than to complain.
		return false;
	}

	GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);
	GEditor->SelectActor(Actor, /*bInSelected=*/true, /*bNotify=*/true);
	GEditor->MoveViewportCamerasToActor(*Actor, /*bActiveViewportOnly=*/false);
	return true;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMapWardenEditorModule, MapWardenEditor)
