// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file was generated by:
//   tools/json_schema_compiler/compiler.py.
// NOTE: The format of types has changed. 'FooType' is now
//   'chrome.fileSystemProvider.FooType'.
// Please run the closure compiler before committing changes.
// See https://chromium.googlesource.com/chromium/src/+/master/docs/closure_compilation.md

/** @fileoverview Externs generated from namespace: fileSystemProvider */

/**
 * @const
 */
chrome.fileSystemProvider = {};

/**
 * @enum {string}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-ProviderError
 */
chrome.fileSystemProvider.ProviderError = {
  OK: 'OK',
  FAILED: 'FAILED',
  IN_USE: 'IN_USE',
  EXISTS: 'EXISTS',
  NOT_FOUND: 'NOT_FOUND',
  ACCESS_DENIED: 'ACCESS_DENIED',
  TOO_MANY_OPENED: 'TOO_MANY_OPENED',
  NO_MEMORY: 'NO_MEMORY',
  NO_SPACE: 'NO_SPACE',
  NOT_A_DIRECTORY: 'NOT_A_DIRECTORY',
  INVALID_OPERATION: 'INVALID_OPERATION',
  SECURITY: 'SECURITY',
  ABORT: 'ABORT',
  NOT_A_FILE: 'NOT_A_FILE',
  NOT_EMPTY: 'NOT_EMPTY',
  INVALID_URL: 'INVALID_URL',
  IO: 'IO',
};

/**
 * @enum {string}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-OpenFileMode
 */
chrome.fileSystemProvider.OpenFileMode = {
  READ: 'READ',
  WRITE: 'WRITE',
};

/**
 * @enum {string}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-ChangeType
 */
chrome.fileSystemProvider.ChangeType = {
  CHANGED: 'CHANGED',
  DELETED: 'DELETED',
};

/**
 * @enum {string}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-CommonActionId
 */
chrome.fileSystemProvider.CommonActionId = {
  SAVE_FOR_OFFLINE: 'SAVE_FOR_OFFLINE',
  OFFLINE_NOT_NECESSARY: 'OFFLINE_NOT_NECESSARY',
  SHARE: 'SHARE',
};

/**
 * @typedef {{
 *   isDirectory: (boolean|undefined),
 *   name: (string|undefined),
 *   size: (number|undefined),
 *   modificationTime: (Object|undefined),
 *   mimeType: (string|undefined),
 *   thumbnail: (string|undefined)
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-EntryMetadata
 */
chrome.fileSystemProvider.EntryMetadata;

/**
 * @typedef {{
 *   entryPath: string,
 *   recursive: boolean,
 *   lastTag: (string|undefined)
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-Watcher
 */
chrome.fileSystemProvider.Watcher;

/**
 * @typedef {{
 *   openRequestId: number,
 *   filePath: string,
 *   mode: !chrome.fileSystemProvider.OpenFileMode
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-OpenedFile
 */
chrome.fileSystemProvider.OpenedFile;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   displayName: string,
 *   writable: boolean,
 *   openedFilesLimit: number,
 *   openedFiles: !Array<!chrome.fileSystemProvider.OpenedFile>,
 *   supportsNotifyTag: (boolean|undefined),
 *   watchers: !Array<!chrome.fileSystemProvider.Watcher>
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-FileSystemInfo
 */
chrome.fileSystemProvider.FileSystemInfo;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   displayName: string,
 *   writable: (boolean|undefined),
 *   openedFilesLimit: (number|undefined),
 *   supportsNotifyTag: (boolean|undefined),
 *   persistent: (boolean|undefined)
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-MountOptions
 */
chrome.fileSystemProvider.MountOptions;

/**
 * @typedef {{
 *   fileSystemId: string
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-UnmountOptions
 */
chrome.fileSystemProvider.UnmountOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-UnmountRequestedOptions
 */
chrome.fileSystemProvider.UnmountRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   entryPath: string,
 *   isDirectory: boolean,
 *   name: boolean,
 *   size: boolean,
 *   modificationTime: boolean,
 *   mimeType: boolean,
 *   thumbnail: boolean
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-GetMetadataRequestedOptions
 */
chrome.fileSystemProvider.GetMetadataRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   entryPaths: !Array<string>
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-GetActionsRequestedOptions
 */
chrome.fileSystemProvider.GetActionsRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   directoryPath: string,
 *   isDirectory: boolean,
 *   name: boolean,
 *   size: boolean,
 *   modificationTime: boolean,
 *   mimeType: boolean,
 *   thumbnail: boolean
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-ReadDirectoryRequestedOptions
 */
chrome.fileSystemProvider.ReadDirectoryRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   filePath: string,
 *   mode: !chrome.fileSystemProvider.OpenFileMode
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-OpenFileRequestedOptions
 */
chrome.fileSystemProvider.OpenFileRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   openRequestId: number
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-CloseFileRequestedOptions
 */
chrome.fileSystemProvider.CloseFileRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   openRequestId: number,
 *   offset: number,
 *   length: number
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-ReadFileRequestedOptions
 */
chrome.fileSystemProvider.ReadFileRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   directoryPath: string,
 *   recursive: boolean
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-CreateDirectoryRequestedOptions
 */
chrome.fileSystemProvider.CreateDirectoryRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   entryPath: string,
 *   recursive: boolean
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-DeleteEntryRequestedOptions
 */
chrome.fileSystemProvider.DeleteEntryRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   filePath: string
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-CreateFileRequestedOptions
 */
chrome.fileSystemProvider.CreateFileRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   sourcePath: string,
 *   targetPath: string
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-CopyEntryRequestedOptions
 */
chrome.fileSystemProvider.CopyEntryRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   sourcePath: string,
 *   targetPath: string
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-MoveEntryRequestedOptions
 */
chrome.fileSystemProvider.MoveEntryRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   filePath: string,
 *   length: number
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-TruncateRequestedOptions
 */
chrome.fileSystemProvider.TruncateRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   openRequestId: number,
 *   offset: number,
 *   data: ArrayBuffer
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-WriteFileRequestedOptions
 */
chrome.fileSystemProvider.WriteFileRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   operationRequestId: number
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-AbortRequestedOptions
 */
chrome.fileSystemProvider.AbortRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   entryPath: string,
 *   recursive: boolean
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-AddWatcherRequestedOptions
 */
chrome.fileSystemProvider.AddWatcherRequestedOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   entryPath: string,
 *   recursive: boolean
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-RemoveWatcherRequestedOptions
 */
chrome.fileSystemProvider.RemoveWatcherRequestedOptions;

/**
 * @typedef {{
 *   id: string,
 *   title: (string|undefined)
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-Action
 */
chrome.fileSystemProvider.Action;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number,
 *   entryPaths: !Array<string>,
 *   actionId: string
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-ExecuteActionRequestedOptions
 */
chrome.fileSystemProvider.ExecuteActionRequestedOptions;

/**
 * @typedef {{
 *   entryPath: string,
 *   changeType: !chrome.fileSystemProvider.ChangeType
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-Change
 */
chrome.fileSystemProvider.Change;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   observedPath: string,
 *   recursive: boolean,
 *   changeType: !chrome.fileSystemProvider.ChangeType,
 *   changes: (!Array<!chrome.fileSystemProvider.Change>|undefined),
 *   tag: (string|undefined)
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-NotifyOptions
 */
chrome.fileSystemProvider.NotifyOptions;

/**
 * @typedef {{
 *   fileSystemId: string,
 *   requestId: number
 * }}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#type-ConfigureRequestedOptions
 */
chrome.fileSystemProvider.ConfigureRequestedOptions;

/**
 * <p>Mounts a file system with the given <code>fileSystemId</code> and
 * <code>displayName</code>. <code>displayName</code> will be shown in the left
 * panel of the Files app. <code>displayName</code> can contain any characters
 * including '/', but cannot be an empty string. <code>displayName</code> must
 * be descriptive but doesn't have to be unique. The <code>fileSystemId</code>
 * must not be an empty string.</p><p>Depending on the type of the file system
 * being mounted, the <code>source</code> option must be set
 * appropriately.</p><p>In case of an error, $(ref:runtime.lastError) will be
 * set with a corresponding error code.</p>
 * @param {!chrome.fileSystemProvider.MountOptions} options
 * @param {function():void=} callback A generic result callback to indicate
 *     success or failure.
 * @see https://developer.chrome.com/extensions/fileSystemProvider#method-mount
 */
chrome.fileSystemProvider.mount = function(options, callback) {};

/**
 * <p>Unmounts a file system with the given <code>fileSystemId</code>. It must
 * be called after $(ref:onUnmountRequested) is invoked. Also, the providing
 * extension can decide to perform unmounting if not requested (eg. in case of
 * lost connection, or a file error).</p><p>In case of an error,
 * $(ref:runtime.lastError) will be set with a corresponding error code.</p>
 * @param {!chrome.fileSystemProvider.UnmountOptions} options
 * @param {function():void=} callback A generic result callback to indicate
 *     success or failure.
 * @see https://developer.chrome.com/extensions/fileSystemProvider#method-unmount
 */
chrome.fileSystemProvider.unmount = function(options, callback) {};

/**
 * Returns all file systems mounted by the extension.
 * @param {function(!Array<!chrome.fileSystemProvider.FileSystemInfo>):void}
 *     callback Callback to receive the result of $(ref:getAll) function.
 * @see https://developer.chrome.com/extensions/fileSystemProvider#method-getAll
 */
chrome.fileSystemProvider.getAll = function(callback) {};

/**
 * Returns information about a file system with the passed
 * <code>fileSystemId</code>.
 * @param {string} fileSystemId
 * @param {function(!chrome.fileSystemProvider.FileSystemInfo):void} callback
 *     Callback to receive the result of $(ref:get) function.
 * @see https://developer.chrome.com/extensions/fileSystemProvider#method-get
 */
chrome.fileSystemProvider.get = function(fileSystemId, callback) {};

/**
 * <p>Notifies about changes in the watched directory at
 * <code>observedPath</code> in <code>recursive</code> mode. If the file system
 * is mounted with <code>supportsNofityTag</code>, then <code>tag</code> must be
 * provided, and all changes since the last notification always reported, even
 * if the system was shutdown. The last tag can be obtained with
 * $(ref:getAll).</p><p>To use, the <code>file_system_provider.notify</code>
 * manifest option must be set to true.</p><p>Value of <code>tag</code> can be
 * any string which is unique per call, so it's possible to identify the last
 * registered notification. Eg. if the providing extension starts after a
 * reboot, and the last registered notification's tag is equal to "123", then it
 * should call $(ref:notify) for all changes which happened since the change
 * tagged as "123". It cannot be an empty string.</p><p>Not all providers are
 * able to provide a tag, but if the file system has a changelog, then the tag
 * can be eg. a change number, or a revision number.</p><p>Note that if a parent
 * directory is removed, then all descendant entries are also removed, and if
 * they are watched, then the API must be notified about the fact. Also, if a
 * directory is renamed, then all descendant entries are in fact removed, as
 * there is no entry under their original paths anymore.</p><p>In case of an
 * error, $(ref:runtime.lastError) will be set will a corresponding error
 * code.</p>
 * @param {!chrome.fileSystemProvider.NotifyOptions} options
 * @param {function():void=} callback A generic result callback to indicate
 *     success or failure.
 * @see https://developer.chrome.com/extensions/fileSystemProvider#method-notify
 */
chrome.fileSystemProvider.notify = function(options, callback) {};

/**
 * Raised when unmounting for the file system with the <code>fileSystemId</code>
 * identifier is requested. In the response, the $(ref:unmount) API method must
 * be called together with <code>successCallback</code>. If unmounting is not
 * possible (eg. due to a pending operation), then <code>errorCallback</code>
 * must be called.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onUnmountRequested
 */
chrome.fileSystemProvider.onUnmountRequested;

/**
 * Raised when metadata of a file or a directory at <code>entryPath</code> is
 * requested. The metadata must be returned with the
 * <code>successCallback</code> call. In case of an error,
 * <code>errorCallback</code> must be called.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onGetMetadataRequested
 */
chrome.fileSystemProvider.onGetMetadataRequested;

/**
 * Raised when a list of actions for a set of files or directories at
 * <code>entryPaths</code> is requested. All of the returned actions must be
 * applicable to each entry. If there are no such actions, an empty array should
 * be returned. The actions must be returned with the
 * <code>successCallback</code> call. In case of an error,
 * <code>errorCallback</code> must be called.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onGetActionsRequested
 */
chrome.fileSystemProvider.onGetActionsRequested;

/**
 * Raised when contents of a directory at <code>directoryPath</code> are
 * requested. The results must be returned in chunks by calling the
 * <code>successCallback</code> several times. In case of an error,
 * <code>errorCallback</code> must be called.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onReadDirectoryRequested
 */
chrome.fileSystemProvider.onReadDirectoryRequested;

/**
 * Raised when opening a file at <code>filePath</code> is requested. If the file
 * does not exist, then the operation must fail. Maximum number of files opened
 * at once can be specified with <code>MountOptions</code>.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onOpenFileRequested
 */
chrome.fileSystemProvider.onOpenFileRequested;

/**
 * Raised when opening a file previously opened with <code>openRequestId</code>
 * is requested to be closed.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onCloseFileRequested
 */
chrome.fileSystemProvider.onCloseFileRequested;

/**
 * Raised when reading contents of a file opened previously with
 * <code>openRequestId</code> is requested. The results must be returned in
 * chunks by calling <code>successCallback</code> several times. In case of an
 * error, <code>errorCallback</code> must be called.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onReadFileRequested
 */
chrome.fileSystemProvider.onReadFileRequested;

/**
 * Raised when creating a directory is requested. The operation must fail with
 * the EXISTS error if the target directory already exists. If
 * <code>recursive</code> is true, then all of the missing directories on the
 * directory path must be created.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onCreateDirectoryRequested
 */
chrome.fileSystemProvider.onCreateDirectoryRequested;

/**
 * Raised when deleting an entry is requested. If <code>recursive</code> is
 * true, and the entry is a directory, then all of the entries inside must be
 * recursively deleted as well.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onDeleteEntryRequested
 */
chrome.fileSystemProvider.onDeleteEntryRequested;

/**
 * Raised when creating a file is requested. If the file already exists, then
 * <code>errorCallback</code> must be called with the <code>"EXISTS"</code>
 * error code.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onCreateFileRequested
 */
chrome.fileSystemProvider.onCreateFileRequested;

/**
 * Raised when copying an entry (recursively if a directory) is requested. If an
 * error occurs, then <code>errorCallback</code> must be called.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onCopyEntryRequested
 */
chrome.fileSystemProvider.onCopyEntryRequested;

/**
 * Raised when moving an entry (recursively if a directory) is requested. If an
 * error occurs, then <code>errorCallback</code> must be called.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onMoveEntryRequested
 */
chrome.fileSystemProvider.onMoveEntryRequested;

/**
 * Raised when truncating a file to a desired length is requested. If an error
 * occurs, then <code>errorCallback</code> must be called.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onTruncateRequested
 */
chrome.fileSystemProvider.onTruncateRequested;

/**
 * Raised when writing contents to a file opened previously with
 * <code>openRequestId</code> is requested.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onWriteFileRequested
 */
chrome.fileSystemProvider.onWriteFileRequested;

/**
 * Raised when aborting an operation with <code>operationRequestId</code> is
 * requested. The operation executed with <code>operationRequestId</code> must
 * be immediately stopped and <code>successCallback</code> of this abort request
 * executed. If aborting fails, then <code>errorCallback</code> must be called.
 * Note, that callbacks of the aborted operation must not be called, as they
 * will be ignored. Despite calling <code>errorCallback</code>, the request may
 * be forcibly aborted.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onAbortRequested
 */
chrome.fileSystemProvider.onAbortRequested;

/**
 * Raised when showing a configuration dialog for <code>fileSystemId</code> is
 * requested. If it's handled, the
 * <code>file_system_provider.configurable</code> manfiest option must be set to
 * true.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onConfigureRequested
 */
chrome.fileSystemProvider.onConfigureRequested;

/**
 * Raised when showing a dialog for mounting a new file system is requested. If
 * the extension/app is a file handler, then this event shouldn't be handled.
 * Instead <code>app.runtime.onLaunched</code> should be handled in order to
 * mount new file systems when a file is opened. For multiple mounts, the
 * <code>file_system_provider.multiple_mounts</code> manifest option must be set
 * to true.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onMountRequested
 */
chrome.fileSystemProvider.onMountRequested;

/**
 * Raised when setting a new directory watcher is requested. If an error occurs,
 * then <code>errorCallback</code> must be called.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onAddWatcherRequested
 */
chrome.fileSystemProvider.onAddWatcherRequested;

/**
 * Raised when the watcher should be removed. If an error occurs, then
 * <code>errorCallback</code> must be called.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onRemoveWatcherRequested
 */
chrome.fileSystemProvider.onRemoveWatcherRequested;

/**
 * Raised when executing an action for a set of files or directories is\
 * requested. After the action is completed, <code>successCallback</code> must
 * be called. On error, <code>errorCallback</code> must be called.
 * @type {!ChromeEvent}
 * @see https://developer.chrome.com/extensions/fileSystemProvider#event-onExecuteActionRequested
 */
chrome.fileSystemProvider.onExecuteActionRequested;
