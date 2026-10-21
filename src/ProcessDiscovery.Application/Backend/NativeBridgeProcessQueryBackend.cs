using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace ProcessDiscovery.Application.Backend;

public sealed class NativeBridgeProcessQueryBackend : IProcessQueryBackend, IDisposable
{
    private const uint AbiVersion = 1;
    private const uint Ok = 0;
    private const uint Cancelled = 6;
    private const uint BufferTooSmall = 7;

    private readonly NativeBridge _bridge;
    private readonly SafeCatalogHandle _catalog;

    public NativeBridgeProcessQueryBackend(string bridgePath)
    {
        _bridge = NativeBridge.Load(bridgePath);
        _catalog = _bridge.CreateCatalog();
    }

    public async Task<BackendResponse> QueryAsync(
        QueryRequest request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();

        using var cancellation = _bridge.CreateCancellation();
        using var registration = cancellationToken.Register(cancellation.Request);
        var options = new NativeOptions
        {
            StructSize = (uint)Marshal.SizeOf<NativeOptions>(),
            AbiVersion = AbiVersion,
            IncludeImagePath = request.IncludeImagePath ? (byte)1 : (byte)0,
            IncludeInternalName = request.IncludeInternalName ? (byte)1 : (byte)0,
            IncludeSignature = request.IncludeSignature ? (byte)1 : (byte)0,
            IncludeModules = request.IncludeModules ? (byte)1 : (byte)0,
            AllowRemoteFileInspection = request.AllowRemoteFileInspection ? (byte)1 : (byte)0,
            Cancellation = cancellation.Handle
        };
        SafeResultHandle? result = null;
        try
        {
            var queryResult = await Task.Run(() =>
            {
                var status = request.Mode == QueryMode.ByPid
                    ? _bridge.QueryByPid(_catalog, request.ProcessId!.Value, ref options, out var nativeResult)
                    : _bridge.QueryEnumerate(_catalog, ref options, out nativeResult);
                return (status, nativeResult);
            }, CancellationToken.None).ConfigureAwait(false);
            var status = queryResult.status;
            result = queryResult.nativeResult;
            if (status == Cancelled)
                throw new OperationCanceledException(cancellationToken);
            if (status != Ok || result is null || result.IsInvalid)
                throw new NativeBridgeException(status, "A consulta nativa falhou.");

            var json = _bridge.CopyJson(result);
            return await new JsonProcessQueryBackend(
                (_, _) => Task.FromResult(json)).QueryAsync(request, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            result?.Dispose();
        }
    }

    public void Dispose()
    {
        _catalog.Dispose();
        _bridge.Dispose();
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct NativeOptions
    {
        public uint StructSize;
        public uint AbiVersion;
        public byte IncludeImagePath;
        public byte IncludeInternalName;
        public byte IncludeSignature;
        public byte IncludeModules;
        public byte AllowRemoteFileInspection;
        public byte Reserved0;
        public byte Reserved1;
        public byte Reserved2;
        public IntPtr Cancellation;
    }

    private sealed class NativeBridge : IDisposable
    {
        private readonly nint _library;
        private readonly CreateCatalogDelegate _createCatalog;
        private readonly FreeCatalogDelegate _freeCatalog;
        private readonly CreateCancellationDelegate _createCancellation;
        private readonly RequestCancellationDelegate _requestCancellation;
        private readonly FreeCancellationDelegate _freeCancellation;
        private readonly QueryEnumerateDelegate _queryEnumerate;
        private readonly QueryByPidDelegate _queryByPid;
        private readonly JsonSizeDelegate _jsonSize;
        private readonly JsonCopyDelegate _jsonCopy;
        private readonly FreeResultDelegate _freeResult;

        private NativeBridge(nint library)
        {
            _library = library;
            _createCatalog = Get<CreateCatalogDelegate>("pd_catalog_create_v1");
            _freeCatalog = Get<FreeCatalogDelegate>("pd_catalog_free_v1");
            _createCancellation = Get<CreateCancellationDelegate>("pd_cancellation_create_v1");
            _requestCancellation = Get<RequestCancellationDelegate>("pd_cancellation_request_v1");
            _freeCancellation = Get<FreeCancellationDelegate>("pd_cancellation_free_v1");
            _queryEnumerate = Get<QueryEnumerateDelegate>("pd_query_enumerate_v1");
            _queryByPid = Get<QueryByPidDelegate>("pd_query_by_pid_v1");
            _jsonSize = Get<JsonSizeDelegate>("pd_result_json_size_v1");
            _jsonCopy = Get<JsonCopyDelegate>("pd_result_copy_json_v1");
            _freeResult = Get<FreeResultDelegate>("pd_result_free_v1");
        }

        public static NativeBridge Load(string path)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(path);
            var library = NativeLibrary.Load(path);
            try { return new NativeBridge(library); }
            catch { NativeLibrary.Free(library); throw; }
        }

        public SafeCatalogHandle CreateCatalog()
        {
            var status = _createCatalog(AbiVersion, out var handle, IntPtr.Zero);
            if (status != Ok || handle == IntPtr.Zero)
                throw new NativeBridgeException(status, "Não foi possível criar o catálogo nativo.");
            return new SafeCatalogHandle(handle, value => _freeCatalog(value));
        }

        public SafeCancellationHandle CreateCancellation()
        {
            var status = _createCancellation(out var handle);
            if (status != Ok || handle == IntPtr.Zero)
                throw new NativeBridgeException(status, "Não foi possível criar o cancelamento nativo.");
            return new SafeCancellationHandle(
                handle,
                value => _requestCancellation(value),
                value => _freeCancellation(value));
        }

        public uint QueryEnumerate(SafeCatalogHandle catalog, ref NativeOptions options, out SafeResultHandle? result)
        {
            var status = _queryEnumerate(catalog.DangerousGetHandle(), ref options, out var raw);
            result = status == Ok && raw != IntPtr.Zero
                ? new SafeResultHandle(raw, value => _freeResult(value))
                : null;
            return status;
        }

        public uint QueryByPid(SafeCatalogHandle catalog, uint pid, ref NativeOptions options, out SafeResultHandle? result)
        {
            var status = _queryByPid(catalog.DangerousGetHandle(), pid, ref options, out var raw);
            result = status == Ok && raw != IntPtr.Zero
                ? new SafeResultHandle(raw, value => _freeResult(value))
                : null;
            return status;
        }

        public string CopyJson(SafeResultHandle result)
        {
            var status = _jsonSize(result.DangerousGetHandle(), out var size);
            if (status != Ok || size > int.MaxValue)
                throw new NativeBridgeException(status, "Não foi possível obter o tamanho do JSON.");

            var buffer = new byte[(int)size];
            status = _jsonCopy(result.DangerousGetHandle(), buffer, (ulong)buffer.Length, out var written);
            if (status == BufferTooSmall)
            {
                buffer = new byte[checked((int)written)];
                status = _jsonCopy(result.DangerousGetHandle(), buffer, (ulong)buffer.Length, out written);
            }
            if (status != Ok)
                throw new NativeBridgeException(status, "Não foi possível copiar o JSON.");
            return Encoding.UTF8.GetString(buffer, 0, checked((int)written)).TrimEnd('\0');
        }

        public void Dispose() => NativeLibrary.Free(_library);

        private T Get<T>(string name) where T : Delegate =>
            Marshal.GetDelegateForFunctionPointer<T>(NativeLibrary.GetExport(_library, name));

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint CreateCatalogDelegate(uint abi, out IntPtr catalog, IntPtr error);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint FreeCatalogDelegate(IntPtr catalog);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint CreateCancellationDelegate(out IntPtr cancellation);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void RequestCancellationDelegate(IntPtr cancellation);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint FreeCancellationDelegate(IntPtr cancellation);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint QueryEnumerateDelegate(IntPtr catalog, ref NativeOptions options, out IntPtr result);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint QueryByPidDelegate(IntPtr catalog, uint pid, ref NativeOptions options, out IntPtr result);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint JsonSizeDelegate(IntPtr result, out ulong size);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint JsonCopyDelegate(IntPtr result, byte[] buffer, ulong capacity, out ulong written);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void FreeResultDelegate(IntPtr result);
    }

    private sealed class SafeCatalogHandle : SafeHandle
    {
        private readonly Func<nint, uint> _release;

        public SafeCatalogHandle(nint handle, Func<nint, uint> release)
            : base(IntPtr.Zero, true)
        {
            _release = release;
            SetHandle(handle);
        }

        public override bool IsInvalid => handle == IntPtr.Zero || handle == new IntPtr(-1);
        protected override bool ReleaseHandle() => _release(handle) == Ok;
    }

    private sealed class SafeResultHandle : SafeHandle
    {
        private readonly Action<nint> _release;

        public SafeResultHandle(nint handle, Action<nint> release)
            : base(IntPtr.Zero, true)
        {
            _release = release;
            SetHandle(handle);
        }

        public override bool IsInvalid => handle == IntPtr.Zero || handle == new IntPtr(-1);
        protected override bool ReleaseHandle() { _release(handle); return true; }
    }

    private sealed class SafeCancellationHandle : SafeHandle
    {
        private readonly Action<nint> _request;
        private readonly Func<nint, uint> _release;

        public SafeCancellationHandle(
            nint handle,
            Action<nint> request,
            Func<nint, uint> release)
            : base(IntPtr.Zero, true)
        {
            _request = request;
            _release = release;
            SetHandle(handle);
        }

        public override bool IsInvalid => handle == IntPtr.Zero || handle == new IntPtr(-1);
        public IntPtr Handle => DangerousGetHandle();
        public void Request() => _request(handle);
        protected override bool ReleaseHandle() => _release(handle) == Ok;
    }
}

public sealed class NativeBridgeException(uint status, string message)
    : Win32Exception(unchecked((int)status), message);
