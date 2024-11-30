import TradingPanel from "./components/TradingPanel";

export default function Home() {
  return (
    <main className='container mx-auto'>
      <h1 className='text-3xl font-bold mb-8 p-6 bg-gray-800 text-white'>
        Deribit Trading Dashboard
      </h1>
      <TradingPanel />
    </main>
  );
}
